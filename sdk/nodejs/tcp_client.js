'use strict';

const net = require('node:net');
const { GateKeeperError, RateLimitError, QuotaError } = require('./errors');

class GateKeeperTcpClient {
  constructor(options = {}) {
    if (typeof options === 'string') {
      const parts = options.replace(/^tcp:\/\//, '').split(':');
      options = { host: parts[0], port: parseInt(parts[1] || '63779', 10) };
    }
    this.host = options.host || '127.0.0.1';
    this.port = options.port || 63779;
    this.timeoutMs = options.timeoutMs || 5000;

    this.socket = null;
    this.buffer = Buffer.alloc(0);
    this.pendingRequests = new Map();
    this.reqIdCounter = 0;
  }

  _connect() {
    return new Promise((resolve, reject) => {
      if (this.socket && !this.socket.destroyed) {
        return resolve(this.socket);
      }

      const socket = net.createConnection({ host: this.host, port: this.port }, () => {
        socket.setKeepAlive(true);
        this.socket = socket;
        resolve(socket);
      });

      socket.setTimeout(this.timeoutMs);

      socket.on('data', (chunk) => {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        this._processBuffer();
      });

      socket.on('error', (err) => {
        for (const [id, req] of this.pendingRequests.entries()) {
          req.reject(err);
          this.pendingRequests.delete(id);
        }
        reject(err);
      });

      socket.on('close', () => {
        this.socket = null;
      });

      socket.on('timeout', () => {
        socket.destroy(new Error('GateKeeper TCP connection timeout'));
      });
    });
  }

  _processBuffer() {
    while (this.buffer.length >= 4) {
      const payloadLength = this.buffer.readUInt32BE(0);
      if (this.buffer.length < 4 + payloadLength) {
        break;
      }

      const payloadBuf = this.buffer.subarray(4, 4 + payloadLength);
      this.buffer = this.buffer.subarray(4 + payloadLength);

      try {
        const resp = JSON.parse(payloadBuf.toString('utf-8'));
        if (resp.id && this.pendingRequests.has(resp.id)) {
          const { resolve, reject, timer } = this.pendingRequests.get(resp.id);
          clearTimeout(timer);
          this.pendingRequests.delete(resp.id);

          if (resp.ok) {
            resolve(resp.result);
          } else {
            const err = new GateKeeperError(
              (resp.error && resp.error.message) || 'GKWP command failed',
              400,
              resp.error
            );
            reject(err);
          }
        }
      } catch (e) {
        // ignore malformed buffer chunk
      }
    }
  }

  async send(op, body = {}) {
    const socket = await this._connect();
    const reqId = `req_${++this.reqIdCounter}`;

    const requestPayload = JSON.stringify({
      id: reqId,
      op,
      body,
    });

    const bodyBuf = Buffer.from(requestPayload, 'utf-8');
    const frame = Buffer.alloc(4 + bodyBuf.length);
    frame.writeUInt32BE(bodyBuf.length, 0);
    bodyBuf.copy(frame, 4);

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        if (this.pendingRequests.has(reqId)) {
          this.pendingRequests.delete(reqId);
          reject(new Error(`GKWP request ${reqId} timed out after ${this.timeoutMs}ms`));
        }
      }, this.timeoutMs);

      this.pendingRequests.set(reqId, { resolve, reject, timer });

      socket.write(frame, (err) => {
        if (err) {
          clearTimeout(timer);
          this.pendingRequests.delete(reqId);
          reject(err);
        }
      });
    });
  }

  async health() {
    try {
      const res = await this.send('PING', {});
      return !!res && res.pong === true;
    } catch (_) {
      return false;
    }
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

    const res = await this.send('GK.RATE_LIMIT', payload);
    const result = {
      allowed: !!res.allowed,
      remaining: res.remaining !== undefined ? res.remaining : 0,
      retryAfterMs: res.retry_after_ms || 0,
      limit,
      resetEpochSec: 0,
      retryAfterSec: Math.ceil((res.retry_after_ms || 0) / 1000),
    };

    if (!result.allowed) {
      throw new RateLimitError('Rate limit exceeded', result);
    }
    return result;
  }

  async reserveQuota({ key, amount, ttlMs }) {
    const res = await this.send('GK.RESERVE', { key, amount, ttl_ms: ttlMs });
    const result = {
      reserved: !!res.reserved,
      reservationId: res.reservation_id || '',
      remaining: res.remaining || 0,
    };
    if (!result.reserved) {
      throw new QuotaError('Insufficient quota', result);
    }
    return result;
  }

  async commitQuota({ key, reservationId, actualAmount = 0 }) {
    const res = await this.send('GK.COMMIT', {
      key,
      reservation_id: reservationId,
      actual_amount: actualAmount,
    });
    return {
      committed: !!res.committed,
      actualAmount: res.actual_amount,
      refunded: res.refunded,
      remaining: res.remaining,
    };
  }

  async rollbackQuota({ key, reservationId }) {
    const res = await this.send('GK.ROLLBACK', {
      key,
      reservation_id: reservationId,
    });
    return {
      rolledBack: !!res.rolled_back,
      refunded: res.refunded,
      remaining: res.remaining,
    };
  }

  async initQuota({ key, quota, ttlMs = 0 }) {
    const payload = { key, quota, amount: quota };
    if (ttlMs > 0) payload.ttl_ms = ttlMs;
    return this.send('GK.QUOTA_INIT', payload);
  }

  async idemBegin({ key, requestHash, ttlMs, ownerToken }) {
    const body = { key, request_hash: requestHash };
    if (ttlMs !== undefined) body.ttl_ms = ttlMs;
    if (ownerToken !== undefined) body.owner_token = ownerToken;
    return this.send('GK.IDEM_BEGIN', body);
  }

  async idemComplete({ key, ownerToken, responseCode, responseBody }) {
    return this.send('GK.IDEM_COMPLETE', {
      key,
      owner_token: ownerToken,
      response_code: responseCode,
      response_body: responseBody,
    });
  }

  async idemFail({ key, ownerToken, errorMessage }) {
    return this.send('GK.IDEM_FAIL', {
      key,
      owner_token: ownerToken,
      error_message: errorMessage,
    });
  }

  close() {
    if (this.socket && !this.socket.destroyed) {
      this.socket.destroy();
      this.socket = null;
    }
  }
}

module.exports = { GateKeeperTcpClient };
