export declare const GATEKEEPER_OPTIONS = 'GATEKEEPER_OPTIONS';

export interface GateKeeperClientOptions {
  endpoint?: string;
  host?: string;
  port?: number;
  useHttp?: boolean;
  protocol?: 'http' | 'tcp';
  timeoutMs?: number;
}

export interface GateKeeperAsyncOptions {
  imports?: any[];
  useFactory?: (...args: any[]) => Promise<GateKeeperClientOptions> | GateKeeperClientOptions;
  inject?: any[];
}

export declare class GateKeeperService {
  constructor(options?: GateKeeperClientOptions);
  getClient(): any;
  health(): Promise<boolean>;
  checkRateLimit(req: any): Promise<any>;
  reserveQuota(req: any): Promise<any>;
  commitQuota(req: any): Promise<any>;
  rollbackQuota(req: any): Promise<any>;
  initQuota(req: any): Promise<any>;
  idemBegin(req: any): Promise<any>;
  idemComplete(req: any): Promise<any>;
  idemFail(req: any): Promise<any>;
  onModuleDestroy(): void;
  close(): void;
}

export declare class GateKeeperModule {
  static forRoot(options?: GateKeeperClientOptions): any;
  static forRootAsync(asyncOptions: GateKeeperAsyncOptions): any;
}
