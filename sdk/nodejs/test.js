'use strict';

const test = require('node:test');
const assert = require('node:assert');
const http = require('node:http');
const net = require('node:net');

const {
  GateKeeperClient,
  RateLimitError,
  QuotaError,
  createRateLimitMiddleware,
  GateKeeperModule,
  GateKeeperService,
  GATEKEEPER_OPTIONS,
} = require('./index');

test('GateKeeperClient - HTTP Rate Limit and Health Check', async () => {
  let count = 0;
  const server = http.createServer((req, res) => {
    if (req.url === '/healthz') {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ status: 'ok' }));
      return;
    }
    if (req.url === '/v1/rate-limit/check') {
      count++;
      if (count <= 2) {
        res.writeHead(200, {
          'Content-Type': 'application/json',
          'X-RateLimit-Limit': '2',
          'X-RateLimit-Remaining': String(2 - count),
          'X-RateLimit-Reset': '1700000060',
        });
        res.end(JSON.stringify({
          allowed: true,
          remaining: 2 - count,
          retry_after_ms: 0,
        }));
      } else {
        res.writeHead(429, {
          'Content-Type': 'application/json',
          'X-RateLimit-Limit': '2',
          'X-RateLimit-Remaining': '0',
          'Retry-After': '60',
        });
        res.end(JSON.stringify({
          allowed: false,
          remaining: 0,
          retry_after_ms: 60000,
        }));
      }
      return;
    }
    res.writeHead(404);
    res.end();
  });

  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  const client = new GateKeeperClient({ endpoint: `http://127.0.0.1:${port}` });

  try {
    const isHealthy = await client.health();
    assert.strictEqual(isHealthy, true);

    const r1 = await client.checkRateLimit({ key: 'user_node', limit: 2, windowMs: 60000 });
    assert.strictEqual(r1.allowed, true);
    assert.strictEqual(r1.remaining, 1);
    assert.strictEqual(r1.limit, 2);

    const r2 = await client.checkRateLimit({ key: 'user_node', limit: 2, windowMs: 60000 });
    assert.strictEqual(r2.allowed, true);
    assert.strictEqual(r2.remaining, 0);

    await assert.rejects(
      async () => {
        await client.checkRateLimit({ key: 'user_node', limit: 2, windowMs: 60000 });
      },
      (err) => {
        assert(err instanceof RateLimitError);
        assert.strictEqual(err.info.allowed, false);
        assert.strictEqual(err.info.remaining, 0);
        assert.strictEqual(err.info.retryAfterMs, 60000);
        return true;
      }
    );
  } finally {
    server.close();
  }
});

test('createRateLimitMiddleware - Express style', async () => {
  let attempts = 0;
  const mockServer = http.createServer((req, res) => {
    attempts++;
    if (attempts === 1) {
      res.writeHead(200, {
        'Content-Type': 'application/json',
        'X-RateLimit-Limit': '1',
        'X-RateLimit-Remaining': '0',
      });
      res.end(JSON.stringify({ allowed: true, remaining: 0, retry_after_ms: 0 }));
    } else {
      res.writeHead(429, {
        'Content-Type': 'application/json',
        'X-RateLimit-Limit': '1',
        'X-RateLimit-Remaining': '0',
        'Retry-After': '30',
      });
      res.end(JSON.stringify({ allowed: false, remaining: 0, retry_after_ms: 30000 }));
    }
  });

  await new Promise((resolve) => mockServer.listen(0, '127.0.0.1', resolve));
  const port = mockServer.address().port;
  const client = new GateKeeperClient({ endpoint: `http://127.0.0.1:${port}` });
  const mw = createRateLimitMiddleware(client, { limit: 1, windowMs: 30000 });

  try {
    let nextCalled1 = false;
    const resHeaders1 = {};
    const req1 = { ip: '192.168.1.10', headers: {} };
    const res1 = {
      setHeader(k, v) { resHeaders1[k] = v; },
      end() {},
    };
    await mw(req1, res1, () => { nextCalled1 = true; });
    assert.strictEqual(nextCalled1, true);
    assert.strictEqual(resHeaders1['X-RateLimit-Remaining'], '0');

    let nextCalled2 = false;
    const resHeaders2 = {};
    let responseBody = '';
    let statusCode = 200;
    const req2 = { ip: '192.168.1.10', headers: {} };
    const res2 = {
      setHeader(k, v) { resHeaders2[k] = v; },
      set statusCode(c) { statusCode = c; },
      get statusCode() { return statusCode; },
      end(body) { responseBody = body; },
    };
    await mw(req2, res2, () => { nextCalled2 = true; });
    assert.strictEqual(nextCalled2, false);
    assert.strictEqual(statusCode, 429);
    assert.strictEqual(resHeaders2['Retry-After'], '30');
    assert(responseBody.includes('"allowed":false'));
  } finally {
    mockServer.close();
  }
});

test('GateKeeperClient - GKWP TCP (Default) Operations', async () => {
  let rateLimitCalls = 0;
  const tcpServer = net.createServer((socket) => {
    let buf = Buffer.alloc(0);
    socket.on('data', (chunk) => {
      buf = Buffer.concat([buf, chunk]);
      while (buf.length >= 4) {
        const len = buf.readUInt32BE(0);
        if (buf.length < 4 + len) break;
        const payloadBuf = buf.subarray(4, 4 + len);
        buf = buf.subarray(4 + len);
        const req = JSON.parse(payloadBuf.toString('utf-8'));

        let resp = { id: req.id, ok: true, result: {} };
        switch (req.op) {
          case 'PING':
            resp.result = { pong: true };
            break;
          case 'GK.RATE_LIMIT':
            rateLimitCalls++;
            if (rateLimitCalls === 1) {
              resp.result = { allowed: true, remaining: 9, retry_after_ms: 0 };
            } else {
              resp.result = { allowed: false, remaining: 0, retry_after_ms: 5000 };
            }
            break;
          case 'GK.QUOTA_INIT':
            resp.result = { ok: true };
            break;
          case 'GK.RESERVE':
            if (req.body.amount > 100) {
              resp.result = { reserved: false, reservation_id: '', remaining: 50 };
            } else {
              resp.result = { reserved: true, reservation_id: 'res_123', remaining: 80 };
            }
            break;
          case 'GK.COMMIT':
            resp.result = { committed: true, actual_amount: req.body.actual_amount, refunded: 5, remaining: 85 };
            break;
          case 'GK.ROLLBACK':
            resp.result = { rolled_back: true, refunded: 20, remaining: 100 };
            break;
          case 'GK.IDEM_BEGIN':
            resp.result = { action: 'PROCEED', owner_token: 'tok_abc' };
            break;
          case 'GK.IDEM_COMPLETE':
            resp.result = { status: 'COMPLETED' };
            break;
          case 'GK.IDEM_FAIL':
            resp.result = { status: 'FAILED' };
            break;
          default:
            resp.ok = false;
            resp.error = { code: 'ERR_UNKNOWN_OP', message: `Unknown op ${req.op}` };
        }

        const respBuf = Buffer.from(JSON.stringify(resp), 'utf-8');
        const frame = Buffer.alloc(4 + respBuf.length);
        frame.writeUInt32BE(respBuf.length, 0);
        respBuf.copy(frame, 4);
        socket.write(frame);
      }
    });
  });

  await new Promise((resolve) => tcpServer.listen(0, '127.0.0.1', resolve));
  const port = tcpServer.address().port;

  const client = new GateKeeperClient({ host: '127.0.0.1', port });

  try {
    const isHealthy = await client.health();
    assert.strictEqual(isHealthy, true);

    const rl1 = await client.checkRateLimit({ key: 'user:1', limit: 10, windowMs: 60000 });
    assert.strictEqual(rl1.allowed, true);
    assert.strictEqual(rl1.remaining, 9);

    await assert.rejects(
      async () => {
        await client.checkRateLimit({ key: 'user:1', limit: 10, windowMs: 60000 });
      },
      (err) => {
        assert(err instanceof RateLimitError);
        assert.strictEqual(err.info.allowed, false);
        return true;
      }
    );

    await client.initQuota({ key: 'quota:api', quota: 100 });

    const q1 = await client.reserveQuota({ key: 'quota:api', amount: 20, ttlMs: 30000 });
    assert.strictEqual(q1.reserved, true);
    assert.strictEqual(q1.reservationId, 'res_123');

    await assert.rejects(
      async () => {
        await client.reserveQuota({ key: 'quota:api', amount: 200, ttlMs: 30000 });
      },
      (err) => {
        assert(err instanceof QuotaError);
        assert.strictEqual(err.info.reserved, false);
        return true;
      }
    );

    const c1 = await client.commitQuota({ key: 'quota:api', reservationId: 'res_123', actualAmount: 15 });
    assert.strictEqual(c1.committed, true);
    assert.strictEqual(c1.refunded, 5);

    const rb1 = await client.rollbackQuota({ key: 'quota:api', reservationId: 'res_123' });
    assert.strictEqual(rb1.rolledBack, true);
    assert.strictEqual(rb1.refunded, 20);

    const idem1 = await client.idemBegin({ key: 'order:1', requestHash: 'h123', ttlMs: 30000 });
    assert.strictEqual(idem1.action, 'PROCEED');
    assert.strictEqual(idem1.owner_token, 'tok_abc');

    const comp1 = await client.idemComplete({ key: 'order:1', ownerToken: 'tok_abc', responseCode: 200, responseBody: '{"id":1}' });
    assert.strictEqual(comp1.status, 'COMPLETED');

    const fail1 = await client.idemFail({ key: 'order:1', ownerToken: 'tok_abc', errorMessage: 'failed' });
    assert.strictEqual(fail1.status, 'FAILED');
  } finally {
    client.close();
    tcpServer.close();
  }
});

test('NestJS GateKeeperModule and GateKeeperService', async () => {
  const dynamicModule = GateKeeperModule.forRoot({ host: '127.0.0.1', port: 63779 });
  assert.strictEqual(dynamicModule.module, GateKeeperModule);
  assert.strictEqual(dynamicModule.exports.length, 1);
  assert.strictEqual(dynamicModule.exports[0], GateKeeperService);

  const optionsProvider = dynamicModule.providers.find((p) => p.provide === GATEKEEPER_OPTIONS);
  assert(optionsProvider);
  assert.strictEqual(optionsProvider.useValue.host, '127.0.0.1');

  const serviceProvider = dynamicModule.providers.find((p) => p.provide === GateKeeperService);
  assert(serviceProvider);
  const service = serviceProvider.useFactory(optionsProvider.useValue);
  assert(service instanceof GateKeeperService);
  assert(service.getClient() instanceof GateKeeperClient);
  service.onModuleDestroy();

  const asyncModule = GateKeeperModule.forRootAsync({
    useFactory: async () => ({ host: '127.0.0.1', port: 63780 }),
  });
  assert.strictEqual(asyncModule.module, GateKeeperModule);
  const asyncOptionsProvider = asyncModule.providers.find((p) => p.provide === GATEKEEPER_OPTIONS);
  assert(asyncOptionsProvider);
  const resolvedOpts = await asyncOptionsProvider.useFactory();
  assert.strictEqual(resolvedOpts.port, 63780);
});
