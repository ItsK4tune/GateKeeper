'use strict';

const { GateKeeperClient } = require('../client');

class GateKeeperService {
  constructor(options = {}) {
    this.client = new GateKeeperClient(options);
  }

  getClient() {
    return this.client;
  }

  health() {
    return this.client.health();
  }

  checkRateLimit(req) {
    return this.client.checkRateLimit(req);
  }

  reserveQuota(req) {
    return this.client.reserveQuota(req);
  }

  commitQuota(req) {
    return this.client.commitQuota(req);
  }

  rollbackQuota(req) {
    return this.client.rollbackQuota(req);
  }

  initQuota(req) {
    return this.client.initQuota(req);
  }

  idemBegin(req) {
    return this.client.idemBegin(req);
  }

  idemComplete(req) {
    return this.client.idemComplete(req);
  }

  idemFail(req) {
    return this.client.idemFail(req);
  }

  onModuleDestroy() {
    if (this.client) {
      this.client.close();
    }
  }

  close() {
    if (this.client) {
      this.client.close();
    }
  }
}

module.exports = { GateKeeperService };
