'use strict';

const crypto = require('crypto');
const { GateKeeperError } = require('./client');

class IdempotencyConflictError extends GateKeeperError {
  constructor(message, info) {
    super(message, 409, info);
    this.name = 'IdempotencyConflictError';
  }
}

function defaultHashCalculator(req, body) {
  const hash = crypto.createHash('sha256');
  hash.update(`${req.method}:${req.url}:`);
  if (body) {
    hash.update(typeof body === 'string' ? body : JSON.stringify(body));
  }
  return hash.digest('hex');
}

function createIdempotencyMiddleware(client, options = {}) {
  const {
    headerName = 'idempotency-key',
    ttlMs = 60000,
    hashCalculator = defaultHashCalculator,
    onConflict = (req, res) => {
      res.statusCode = 409;
      res.setHeader('Content-Type', 'application/json');
      res.end(JSON.stringify({
        error: 'ERR_IDEMPOTENCY_CONFLICT',
        message: 'A request with the same idempotency key but different parameters is already in progress or completed.',
      }));
    },
  } = options;

  return async function idempotencyMiddleware(req, res, next) {
    const key = req.headers ? req.headers[headerName.toLowerCase()] : null;
    if (!key) {
      if (typeof next === 'function') next();
      return;
    }

    const reqHash = hashCalculator(req, req.body);
    try {
      const beginRes = await client.idemBegin({
        key,
        requestHash: reqHash,
        ttlMs,
      });

      if (beginRes.action === 'REPLAY') {
        res.setHeader('X-Cache-Lookup', 'HIT');
        res.setHeader('Content-Type', 'application/json');
        res.statusCode = beginRes.response_code || 200;
        res.end(beginRes.response_body || '');
        return;
      }

      const originalEnd = res.end;
      const chunks = [];
      res.write = function (chunk, ...args) {
        if (chunk) chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
        return true;
      };

      res.end = function (chunk, ...args) {
        if (chunk) chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
        const bodyStr = Buffer.concat(chunks).toString('utf8');

        if (res.statusCode < 400) {
          client.idemComplete({
            key,
            ownerToken: beginRes.owner_token,
            responseCode: res.statusCode,
            responseBody: bodyStr,
          }).catch(() => {});
        } else {
          client.idemFail({
            key,
            ownerToken: beginRes.owner_token,
            errorMessage: bodyStr,
          }).catch(() => {});
        }

        return originalEnd.call(this, Buffer.concat(chunks), ...args);
      };

      if (typeof next === 'function') next();
    } catch (err) {
      if (err instanceof IdempotencyConflictError || err.status === 409) {
        return onConflict(req, res);
      }
      res.statusCode = 500;
      res.setHeader('Content-Type', 'application/json');
      res.end(JSON.stringify({ error: 'GateKeeper idempotency error', message: err.message }));
    }
  };
}

module.exports = {
  IdempotencyConflictError,
  createIdempotencyMiddleware,
  defaultHashCalculator,
};
