'use strict';

const { GateKeeperService } = require('./gatekeeper.service');

const GATEKEEPER_OPTIONS = 'GATEKEEPER_OPTIONS';

class GateKeeperModule {
  static forRoot(options = {}) {
    return {
      module: GateKeeperModule,
      providers: [
        {
          provide: GATEKEEPER_OPTIONS,
          useValue: options,
        },
        {
          provide: GateKeeperService,
          useFactory: (opts) => new GateKeeperService(opts),
          inject: [GATEKEEPER_OPTIONS],
        },
      ],
      exports: [GateKeeperService],
    };
  }

  static forRootAsync(asyncOptions = {}) {
    return {
      module: GateKeeperModule,
      imports: asyncOptions.imports || [],
      providers: [
        {
          provide: GATEKEEPER_OPTIONS,
          useFactory: asyncOptions.useFactory,
          inject: asyncOptions.inject || [],
        },
        {
          provide: GateKeeperService,
          useFactory: (opts) => new GateKeeperService(opts),
          inject: [GATEKEEPER_OPTIONS],
        },
      ],
      exports: [GateKeeperService],
    };
  }
}

module.exports = {
  GateKeeperModule,
  GateKeeperService,
  GATEKEEPER_OPTIONS,
};
