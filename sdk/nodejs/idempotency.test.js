'use strict';

const http = require('http');
const { GateKeeperClient } = require('./client');
const { createIdempotencyMiddleware } = require('./idempotency');

async function runTests() {
  let completed = false;
  const mockServer = http.createServer((req, res) => {
    let body = '';
    req.on('data', chunk => { body += chunk; });
    req.on('end', () => {
      res.setHeader('Content-Type', 'application/json');
      if (req.url === '/v1/idempotency/begin') {
        if (completed) {
          res.writeHead(200);
          res.end(JSON.stringify({
            action: 'REPLAY',
            owner_token: 'tok_node',
            response_code: 201,
            response_body: '{"id":"node_123"}',
          }));
          return;
        }
        res.writeHead(200);
        res.end(JSON.stringify({
          action: 'EXECUTE',
          owner_token: 'tok_node',
        }));
        return;
      }
      if (req.url === '/v1/idempotency/complete') {
        completed = true;
        res.writeHead(200);
        res.end(JSON.stringify({ completed: true }));
        return;
      }
      res.writeHead(404);
      res.end('{}');
    });
  });

  await new Promise(resolve => mockServer.listen(0, '127.0.0.1', resolve));
  const port = mockServer.address().port;
  const client = new GateKeeperClient({ endpoint: `http://127.0.0.1:${port}` });

  try {
    // 1. Test Client methods
    const beginRes = await client.idemBegin({ key: 'test_node_1', requestHash: 'hash_node_1', ttlMs: 60000 });
    if (beginRes.action !== 'EXECUTE') {
      throw new Error(`expected EXECUTE, got ${beginRes.action}`);
    }

    const compRes = await client.idemComplete({
      key: 'test_node_1',
      ownerToken: beginRes.owner_token,
      responseCode: 201,
      responseBody: '{"id":"node_123"}',
    });
    if (!compRes.completed) {
      throw new Error('expected completed: true');
    }

    const replayRes = await client.idemBegin({ key: 'test_node_1', requestHash: 'hash_node_1' });
    if (replayRes.action !== 'REPLAY') {
      throw new Error(`expected REPLAY, got ${replayRes.action}`);
    }

    // 2. Test Middleware
    const mw = createIdempotencyMiddleware(client);
    let handlerExecuted = 0;
    const fakeReq = {
      method: 'POST',
      url: '/items',
      headers: { 'idempotency-key': 'test_node_1' },
      body: { name: 'item' },
    };
    let writtenData = '';
    const fakeRes = {
      setHeader: () => {},
      end: (data) => { writtenData = data; },
      statusCode: 200,
    };

    await mw(fakeReq, fakeRes, () => {
      handlerExecuted++;
    });

    if (handlerExecuted !== 0) {
      throw new Error(`Handler should not be executed on REPLAY! executed=${handlerExecuted}`);
    }
    if (writtenData !== '{"id":"node_123"}') {
      throw new Error(`Cached data mismatch: ${writtenData}`);
    }

    console.log('Node.js SDK idempotency tests passed!');
  } finally {
    mockServer.close();
  }
}

runTests().catch(err => {
  console.error('Node.js idempotency test failed:', err);
  process.exit(1);
});
