'use strict';

class GateKeeperError extends Error {
  constructor(message, status = 400, data = {}) {
    super(message);
    this.name = 'GateKeeperError';
    this.status = status;
    this.data = data;
  }
}

class RateLimitError extends GateKeeperError {
  constructor(message, info = {}) {
    super(message, 429, info);
    this.name = 'RateLimitError';
    this.info = info;
  }
}

class QuotaError extends GateKeeperError {
  constructor(message, info = {}) {
    super(message, 429, info);
    this.name = 'QuotaError';
    this.info = info;
  }
}

module.exports = {
  GateKeeperError,
  RateLimitError,
  QuotaError,
};
