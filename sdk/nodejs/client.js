'use strict';

class GateKeeperError extends Error {
  constructor(message, status, data) {
    super(message);
    this.name = 'GateKeeperError';
    this.status = status;
    this.data = data;
  }
}

class RateLimitError extends GateKeeperError {
  constructor(message, info) {
    super(message, 429, info);
    this.name = 'RateLimitError';
    this.info = info;
  }
}

class QuotaError extends GateKeeperError {
  constructor(message, info) {
    super(message, 429, info);
    this.name = 'QuotaError';
    this.info = info;
  }
}

class GateKeeperClient {
  constructor(options = {}) {
    if (typeof options === 'string') {
      options = { endpoint: options };
    }
    this.endpoint = (options.endpoint || 'http://127.0.0.1:8080').replace(/\/+$/, '');
    this.timeoutMs = options.timeoutMs || 5000;
  }

  async _request(path, method = 'GET', body = null) {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), this.timeoutMs);

    const headers = { 'Content-Type': 'application/json' };
    const config = {
      method,
      headers,
      signal: controller.signal,
    };
    if (body !== null) {
      config.body = JSON.stringify(body);
    }

    try {
      const res = await fetch(`${this.endpoint}${path}`, config);
      const text = await res.text();
      let data = {};
      if (text) {
        try {
          data = JSON.parse(text);
        } catch (_) {
          data = { raw: text };
        }
      }

      return {
        status: res.status,
        headers: res.headers,
        data,
      };
    } finally {
      clearTimeout(timeoutId);
    }
  }

  async health() {
    const res = await this._request('/healthz');
    return res.status === 200 && res.data && res.data.status === 'ok';
  }

  async checkRateLimit({ key, tenant, subject, resource, limit, windowMs, cost = 1 }) {
    const payload = {
      key,
      tenant,
      subject,
      resource,
      limit,
      window_ms: windowMs,
      cost,
    };

    const res = await this._request('/v1/rate-limit/check', 'POST', payload);
    const limitHdr = res.headers.get('x-ratelimit-limit');
    const remainingHdr = res.headers.get('x-ratelimit-remaining');
    const resetHdr = res.headers.get('x-ratelimit-reset');
    const retryAfterHdr = res.headers.get('retry-after');

    const result = {
      allowed: !!res.data.allowed,
      remaining: res.data.remaining !== undefined ? res.data.remaining : (remainingHdr ? parseInt(remainingHdr, 10) : 0),
      retryAfterMs: res.data.retry_after_ms || 0,
      limit: limitHdr ? parseInt(limitHdr, 10) : limit,
      resetEpochSec: resetHdr ? parseInt(resetHdr, 10) : 0,
      retryAfterSec: retryAfterHdr ? parseInt(retryAfterHdr, 10) : 0,
    };

    if (res.status === 429 || !result.allowed) {
      throw new RateLimitError('Rate limit exceeded', result);
    }

    if (res.status !== 200) {
      throw new GateKeeperError(`Rate limit check failed with status ${res.status}`, res.status, res.data);
    }

    return result;
  }

  async reserveQuota({ key, amount, ttlMs }) {
    const res = await this._request('/v1/quota/reserve', 'POST', {
      key,
      amount,
      ttl_ms: ttlMs,
    });

    const result = {
      reserved: !!res.data.reserved,
      reservationId: res.data.reservation_id || '',
      remaining: res.data.remaining || 0,
    };

    if (res.status === 429 || !result.reserved) {
      throw new QuotaError('Insufficient quota', result);
    }

    if (res.status !== 200) {
      throw new GateKeeperError(`Quota reservation failed with status ${res.status}`, res.status, res.data);
    }

    return result;
  }

  async commitQuota({ key, reservationId, actualAmount = 0 }) {
    const res = await this._request('/v1/quota/commit', 'POST', {
      key,
      reservation_id: reservationId,
      actual_amount: actualAmount,
    });

    if (res.status === 404) {
      throw new GateKeeperError('Reservation not found or expired', 404, res.data);
    }

    if (res.status !== 200) {
      throw new GateKeeperError(`Quota commit failed with status ${res.status}`, res.status, res.data);
    }

    return {
      committed: !!res.data.committed,
      actualAmount: res.data.actual_amount,
      refunded: res.data.refunded,
      remaining: res.data.remaining,
    };
  }

  async idemBegin({ key, requestHash, ttlMs, ownerToken }) {
    const body = { key, request_hash: requestHash };
    if (ttlMs !== undefined) body.ttl_ms = ttlMs;
    if (ownerToken !== undefined) body.owner_token = ownerToken;
    const res = await this._request('/v1/idempotency/begin', 'POST', body);
    if (res.status === 409) {
      throw new GateKeeperError(res.data.message || 'Idempotency conflict', 409, res.data);
    }
    if (res.status !== 200) {
      throw new GateKeeperError(`Idempotency begin failed: ${res.data.message || res.status}`, res.status, res.data);
    }
    return res.data;
  }

  async idemComplete({ key, ownerToken, responseCode, responseBody }) {
    const res = await this._request('/v1/idempotency/complete', 'POST', {
      key,
      owner_token: ownerToken,
      response_code: responseCode,
      response_body: responseBody,
    });
    if (res.status !== 200) {
      throw new GateKeeperError(`Idempotency complete failed: ${res.data.message || res.status}`, res.status, res.data);
    }
    return res.data;
  }

  async idemFail({ key, ownerToken, errorMessage }) {
    const res = await this._request('/v1/idempotency/fail', 'POST', {
      key,
      owner_token: ownerToken,
      error_message: errorMessage,
    });
    if (res.status !== 200) {
      throw new GateKeeperError(`Idempotency fail failed: ${res.data.message || res.status}`, res.status, res.data);
    }
    return res.data;
  }

  async rollbackQuota({ key, reservationId }) {
    const res = await this._request('/v1/quota/rollback', 'POST', {
      key,
      reservation_id: reservationId,
    });

    if (res.status === 404) {
      throw new GateKeeperError('Reservation not found or expired', 404, res.data);
    }

    if (res.status !== 200) {
      throw new GateKeeperError(`Quota rollback failed with status ${res.status}`, res.status, res.data);
    }

    return {
      rolledBack: !!res.data.rolled_back,
      refunded: res.data.refunded,
      remaining: res.data.remaining,
    };
  }

  async initQuota({ key, quota, ttlMs = 0 }) {
    const payload = { key, quota };
    if (ttlMs > 0) {
      payload.ttl_ms = ttlMs;
    }
    const res = await this._request('/v1/quota/init', 'POST', payload);
    if (res.status !== 200) {
      throw new GateKeeperError(`Init quota failed with status ${res.status}`, res.status, res.data);
    }
    return res.data;
  }
}

module.exports = {
  GateKeeperClient,
  GateKeeperError,
  RateLimitError,
  QuotaError,
};
