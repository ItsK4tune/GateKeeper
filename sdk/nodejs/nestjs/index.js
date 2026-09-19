'use strict';

const { GateKeeperService } = require('./gatekeeper.service');
const { GateKeeperModule, GATEKEEPER_OPTIONS } = require('./gatekeeper.module');

module.exports = {
  GateKeeperService,
  GateKeeperModule,
  GATEKEEPER_OPTIONS,
};
