'use strict';

const {
  GateKeeperClient,
  GateKeeperHttpClient,
  GateKeeperTcpClient,
  GateKeeperError,
  RateLimitError,
  QuotaError,
} = require('./client');
const { createRateLimitMiddleware, defaultKeyExtractor } = require('./middleware');
const {
  createIdempotencyMiddleware,
  IdempotencyConflictError,
  defaultHashCalculator,
} = require('./idempotency');
const {
  GateKeeperModule,
  GateKeeperService,
  GATEKEEPER_OPTIONS,
} = require('./nestjs');

module.exports = {
  GateKeeperClient,
  GateKeeperHttpClient,
  GateKeeperTcpClient,
  GateKeeperError,
  RateLimitError,
  QuotaError,
  createRateLimitMiddleware,
  defaultKeyExtractor,
  createIdempotencyMiddleware,
  IdempotencyConflictError,
  defaultHashCalculator,
  GateKeeperModule,
  GateKeeperService,
  GATEKEEPER_OPTIONS,
};
