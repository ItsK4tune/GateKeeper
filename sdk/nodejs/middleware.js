'use strict';

const { RateLimitError } = require('./client');

function defaultKeyExtractor(req) {
  if (req.headers && req.headers['x-forwarded-for']) {
    return req.headers['x-forwarded-for'].split(',')[0].trim();
  }
  if (req.headers && req.headers['x-real-ip']) {
    return req.headers['x-real-ip'].trim();
  }
  if (req.ip) return req.ip;
  if (req.socket && req.socket.remoteAddress) return req.socket.remoteAddress;
  return '127.0.0.1';
}

function defaultOnRateLimited(req, res, info) {
  res.setHeader('Content-Type', 'application/json');
  res.statusCode = 429;
  res.end(JSON.stringify({
    error: 'Too Many Requests',
    allowed: false,
    remaining: 0,
    retry_after_ms: info.retryAfterMs,
  }));
}

function createRateLimitMiddleware(client, options = {}) {
  const {
    limit,
    windowMs,
    cost = 1,
    tenant = 'default',
    resource = 'default',
    keyExtractor = defaultKeyExtractor,
    onRateLimited = defaultOnRateLimited,
    onError = null,
    failOpen = false,
  } = options;

  return async function rateLimitMiddleware(req, res, next) {
    const subject = keyExtractor(req);
    try {
      const result = await client.checkRateLimit({
        tenant,
        subject,
        resource,
        limit,
        windowMs,
        cost,
      });

      res.setHeader('X-RateLimit-Limit', String(result.limit));
      res.setHeader('X-RateLimit-Remaining', String(result.remaining));
      if (result.resetEpochSec) {
        res.setHeader('X-RateLimit-Reset', String(result.resetEpochSec));
      }

      if (typeof next === 'function') {
        next();
      }
    } catch (err) {
      if (err instanceof RateLimitError) {
        const info = err.info || {};
        res.setHeader('X-RateLimit-Limit', String(info.limit || limit));
        res.setHeader('X-RateLimit-Remaining', '0');
        if (info.resetEpochSec) {
          res.setHeader('X-RateLimit-Reset', String(info.resetEpochSec));
        }
        if (info.retryAfterMs) {
          res.setHeader('Retry-After', String(Math.ceil(info.retryAfterMs / 1000)));
        }
        return onRateLimited(req, res, info);
      }

      if (onError) {
        return onError(err, req, res, next);
      }
      if (failOpen) {
        if (typeof next === 'function') return next();
        return;
      }

      res.statusCode = 503;
      res.setHeader('Content-Type', 'application/json');
      res.end(JSON.stringify({ error: 'GateKeeper rate limit service error' }));
    }
  };
}

module.exports = {
  createRateLimitMiddleware,
  defaultKeyExtractor,
};
