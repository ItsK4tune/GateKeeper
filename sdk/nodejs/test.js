'use strict';

const test = require('node:test');
const assert = require('node:assert');
const http = require('node:http');

const {
  GateKeeperClient,
  RateLimitError,
  QuotaError,
  createRateLimitMiddleware,
} = require('./index');

test('GateKeeperClient - Rate Limit and Health Check', async () => {
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
    assert.strictEqual(isHealthy, true, 'health should be true');

    // 1st check -> OK
    const r1 = await client.checkRateLimit({ key: 'user_node', limit: 2, windowMs: 60000 });
    assert.strictEqual(r1.allowed, true);
    assert.strictEqual(r1.remaining, 1);
    assert.strictEqual(r1.limit, 2);

    // 2nd check -> OK
    const r2 = await client.checkRateLimit({ key: 'user_node', limit: 2, windowMs: 60000 });
    assert.strictEqual(r2.allowed, true);
    assert.strictEqual(r2.remaining, 0);

    // 3rd check -> Throws RateLimitError (429)
    await assert.rejects(
      async () => {
        await client.checkRateLimit({ key: 'user_node', limit: 2, windowMs: 60000 });
      },
      (err) => {
        assert(err instanceof RateLimitError, 'should be RateLimitError');
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
    // 1st request -> passes to next()
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

    // 2nd request -> rate limited 429
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
    assert.strictEqual(nextCalled2, false, 'next() should not be called when rate limited');
    assert.strictEqual(statusCode, 429);
    assert.strictEqual(resHeaders2['Retry-After'], '30');
    assert(responseBody.includes('"allowed":false'));
  } finally {
    mockServer.close();
  }
});
