# GateKeeper

GateKeeper is a high-performance in-memory key-value store, distributed rate-limiting, and idempotency engine written in C++20. It provides sub-millisecond request validation, atomic counters, two-phase quota reservation with automatic rollback on timeout, an idempotency engine with single-flight request coalescing, pluggable AOF persistence, and dual-protocol connectivity (custom binary TCP protocol GKWP/1 and native HTTP/1.1 REST).

---

## Table of Contents / Mục Lục

- [English Documentation](#english-documentation)
  - [1. Overview & Architecture](#1-overview--architecture)
  - [2. GateKeeper Wire Protocol Specification (GKWP/1)](#2-gatekeeper-wire-protocol-specification-gkwp1)
  - [3. Features & Command Reference](#3-features--command-reference)
    - [A. Core Key-Value Operations](#a-core-key-value-operations)
    - [B. TTL and Expiration Commands](#b-ttl-and-expiration-commands)
    - [C. Atomic Counter Operations](#c-atomic-counter-operations)
    - [D. Sliding Window Counter - Hybrid Rate Limiting](#d-sliding-window-counter---hybrid-rate-limiting)
    - [E. Two-Phase Quota Reservation](#e-two-phase-quota-reservation)
    - [F. Idempotency Engine & Single-Flight Coalescing](#f-idempotency-engine--single-flight-coalescing)
    - [G. Pluggable AOF Persistence & Crash Recovery](#g-pluggable-aof-persistence--crash-recovery)
  - [4. Installation & Build](#4-installation--build)
  - [5. Client SDKs and Middlewares](#5-client-sdks-and-middlewares)
- [Tài Liệu Tiếng Việt](#tài-liệu-tiếng-việt)
  - [1. Giới Thiệu & Kiến Trúc Hệ Thống](#1-giới-thiệu--kiến-trúc-hệ-thống)
  - [2. Đặc Tả Giao Thức GKWP/1](#2-đặc-tả-giao-thức-gkwp1)
  - [3. Tính Năng & Hướng Dẫn Sử Dụng](#3-tính-năng--hướng-dẫn-sử-dụng)
    - [A. Thao Tác Key-Value Cốt Lõi](#a-thao-tác-key-value-cốt-lõi)
    - [B. Nhóm Lệnh TTL và Hết Hạn](#b-nhóm-lệnh-ttl-và-hết-hạn)
    - [C. Bộ Đếm Nguyên Tử (Atomic Counters)](#c-bộ-đếm-nguyên-tử-atomic-counters)
    - [D. Kiểm Soát Tốc Độ Cửa Sổ Trượt - Hybrid](#d-kiểm-soát-tốc-độ-cửa-sổ-trượt---hybrid)
    - [E. Cơ Chế Giữ Chỗ Hạn Ngạch 2 Pha (Two-Phase Quota Reservation)](#e-cơ-chế-giữ-chỗ-hạn-ngạch-2-pha-two-phase-quota-reservation)
    - [F. Động Cơ Xử Lý Idempotency & Gom Nhóm Request (Single-Flight)](#f-động-cơ-xử-lý-idempotency--gom-nhóm-request-single-flight)
    - [G. Lưu Trữ Bền Vững AOF & Phục Hồi Sau Sự Cố](#g-lưu-trữ-bền-vững-aof--phục-hồi-sau-sự-cố)
  - [4. Cài Đặt & Biên Dịch](#4-cài-đặt--biên-dịch)
  - [5. Hướng Dẫn Sử Dụng SDK và Middleware](#5-hướng-dẫn-sử-dụng-sdk-và-middleware)

---

## English Documentation

### 1. Overview & Architecture

GateKeeper is designed for infrastructure architectures requiring centralized, highly consistent rate limiting, resource quota accounting, and idempotent execution across distributed microservices.

Core architectural components:

- **Single-Threaded Non-Blocking Event Loop**: Built on Linux epoll multiplexing with edge/level-triggered socket handling and partial read/write buffering. The entire dataset and counter operations reside in memory, executed sequentially without thread context-switching or mutex contention in the execution path.
- **Zero External Dependencies**: Implemented entirely with the modern C++20 standard library (strict memory safety and RAII), containing a custom RFC 8259 streaming JSON parser (`protocol::JsonReader`) and an internal hash table engine.
- **Dual-Protocol Listener**: The event loop simultaneously services incoming connections on two independent ports:
  1. The custom binary length-prefixed GateKeeper Wire Protocol (GKWP/1) on port 63779.
  2. Native HTTP/1.1 REST API on port 8080.
- **Hybrid Expiration Model**: Combines lazy eviction (evaluated upon access) with active periodic expiration (random key sampling triggered by an integrated timer on the event loop).
- **Hybrid Sliding Window Counter**: Rate limiting that eliminates boundary burst while maintaining $O(1)$ memory and $O(1)$ CPU overhead.
- **Idempotency Engine & Single-Flight Coalescing**: Guarantees exactly-once execution for state-mutating requests, automatically deduplicating concurrent duplicate requests via connection parking.
- **Pluggable AOF Persistence**: Write-ahead append-only log (AOF) with configurable fsync policies (`always`, `everysec`, `no`) and automatic crash recovery on startup.

---

### 2. GateKeeper Wire Protocol Specification (GKWP/1)

The GateKeeper Wire Protocol (version 1) is a lightweight, bidirectional, length-prefixed TCP protocol designed to minimize parsing overhead while retaining structured JSON payloads for application data.

#### Frame Layout

Every transmission across a GKWP connection consists of a 4-byte header followed by the frame payload:

```
+-----------------------------+------------------------------------+
| Length (4 Bytes, Big-Endian)| Payload (N Bytes, UTF-8 JSON)      |
+-----------------------------+------------------------------------+
```

- **Length Field**: A 32-bit unsigned integer in network byte order (big-endian). It specifies the exact byte length of the trailing payload. The maximum allowed frame length is 16 MiB (16,777,216 bytes).
- **Payload**: A valid UTF-8 JSON object representing either a request or a response.

#### Request Frame Format

```json
{
  "id": "req-1001",
  "op": "GK.RATE_LIMIT",
  "body": {
    "key": "ratelimit:tenant_a:user_12",
    "limit": 100,
    "window_ms": 60000,
    "cost": 1
  }
}
```

- `id` (string, required): Client-generated correlation identifier.
- `op` (string, required): Command opcode (case-insensitive in dispatching, canonicalized to uppercase).
- `body` (object, optional): Command arguments formatted as structured JSON fields.

#### Response Frame Format

Success Response:
```json
{
  "id": "req-1001",
  "ok": true,
  "result": {
    "allowed": true,
    "remaining": 99,
    "retry_after_ms": 0
  }
}
```

Error Response:
```json
{
  "id": "req-1001",
  "ok": false,
  "error": {
    "code": "INVALID_ARGUMENTS",
    "message": "limit must be greater than zero"
  }
}
```

Connection semantics support request pipelining over persistent connections. Pipelined responses are returned in strict order of receipt.

---

### 3. Features & Command Reference

#### A. Core Key-Value Operations

##### GKWP Commands

| Command | Syntax | Description |
| :--- | :--- | :--- |
| `PING` | `PING [message]` | Tests connection liveness. Returns `PONG` or echoed message. |
| `SET` | `SET <key> <value> [EX seconds \| PX ms] [NX \| XX]` | Sets key to string value with optional TTL and conditions. |
| `GET` | `GET <key>` | Retrieves the value of key. Returns null if not found or expired. |
| `DEL` | `DEL <key> [key ...]` | Removes specified keys from the database. |
| `EXISTS` | `EXISTS <key>` | Checks if key exists and is unexpired. |
| `TYPE` | `TYPE <key>` | Returns the data type (`string`, `none`). |
| `DBSIZE` | `DBSIZE` | Returns total number of active entries in the database. |
| `KEYS` | `KEYS [pattern]` | Scans keys matching pattern. |
| `SCAN` | `SCAN <cursor> [COUNT count]` | Iterates keys incrementally using cursor. |

##### HTTP REST Endpoints

| Method | Path | Body / Query | Description |
| :--- | :--- | :--- | :--- |
| `POST` | `/v1/kv/set` | `{"key":"k","value":"v","ttl_ms":60000}` | Sets key with optional TTL (`ttl_ms` or `ttl_seconds`). |
| `GET` | `/v1/kv/get` | `?key=k` | Retrieves value of key (returns 404 if not found). |
| `POST` | `/v1/kv/del` | `{"key":"k"}` | Deletes key. Returns `{"deleted": true/false}`. |
| `POST` | `/v1/kv/exists` | `{"key":"k"}` | Checks existence. Returns `{"exists": true/false}`. |
| `GET` | `/v1/kv/type` | `?key=k` | Returns type `{"type": "string"|"none"}`. |

---

#### B. TTL and Expiration Commands

##### GKWP Commands

| Command | Syntax | Description |
| :--- | :--- | :--- |
| `EXPIRE` | `EXPIRE <key> <seconds>` | Sets expiration on key in seconds. |
| `PEXPIRE`| `PEXPIRE <key> <ms>` | Sets expiration on key in milliseconds. |
| `TTL` | `TTL <key>` | Returns remaining TTL in seconds (-1 if no TTL, -2 if not found). |
| `PTTL` | `PTTL <key>` | Returns remaining TTL in milliseconds (-1 if no TTL, -2 if not found). |
| `PERSIST`| `PERSIST <key>` | Removes existing timeout from a key. |

##### HTTP REST Endpoints

| Method | Path | Body / Query | Description |
| :--- | :--- | :--- | :--- |
| `POST` | `/v1/kv/expire` | `{"key":"k","ttl_seconds":60}` | Sets TTL in seconds or milliseconds (`ttl_ms`). |
| `GET` | `/v1/kv/ttl` | `?key=k` | Returns remaining TTL `{"ttl": 60}` (-1 or -2 if none/expired). |

---

#### C. Atomic Counter Operations

- `INCR <key>`: Increments integer value by 1.
- `DECR <key>`: Decrements integer value by 1.
- `INCRBY <key> <delta>`: Atomically increments or decrements integer value by signed 64-bit integer.

---

#### D. Sliding Window Counter - Hybrid Rate Limiting

GateKeeper implements the **Sliding Window Counter - Hybrid** algorithm. It computes a weighted sum of requests between the previous window and the current window:

$$\text{weight} = \frac{\text{window\_ms} - (\text{now\_ms} \bmod \text{window\_ms})}{\text{window\_ms}}$$
$$\text{estimated\_count} = \text{previous\_count} \times \text{weight} + \text{current\_count}$$

- **Zero Boundary Burst**: Smooths out traffic spikes at window transitions.
- **$O(1)$ Memory & CPU**: Requires only two counters per key, avoiding the linear RAM overhead of Sliding Window Log.

##### GKWP Command

`GK.RATE_LIMIT <key> <limit> <window_ms> [cost]`

##### HTTP REST Endpoint

`POST /v1/rate-limit/check`

```bash
curl -i -X POST http://127.0.0.1:8080/v1/rate-limit/check \
  -H "Content-Type: application/json" \
  -d '{
    "tenant": "default",
    "subject": "192.168.1.100",
    "resource": "api:orders",
    "limit": 60,
    "window_ms": 60000,
    "cost": 1
  }'
```

Returns standard RFC headers:
- `X-RateLimit-Limit`: Maximum requests per window.
- `X-RateLimit-Remaining`: Available requests remaining.
- `X-RateLimit-Reset`: Unix timestamp in seconds when the window resets.
- `Retry-After`: Seconds to wait before retrying (sent on status 429).

---

#### E. Two-Phase Quota Reservation

Designed for multi-step workflows where token or credit consumption cannot be predicted upfront (e.g. LLM generation, billing):

1. **Initialize Quota Pool**:
   `GK.QUOTA_INIT <key> <quota> [ttl_ms]` or `POST /v1/quota/init`
2. **Phase 1 (Reserve)**:
   `GK.RESERVE <key> <amount> <ttl_ms>` or `POST /v1/quota/reserve`
   Atomically deducts the requested upper bound and issues a unique `reservation_id`.
3. **Phase 2 (Commit or Rollback)**:
   - `GK.COMMIT <key> <reservation_id> [actual_amount]` or `POST /v1/quota/commit`
     Finalizes actual consumption. If `actual_amount < reserved_amount`, the difference is immediately refunded back to the quota pool.
   - `GK.ROLLBACK <key> <reservation_id>` or `POST /v1/quota/rollback`
     Cancels the reservation and restores 100% of the reserved amount back to the pool.
4. **Auto-Rollback on Timeout**:
   If a worker crashes or fails to commit before `ttl_ms` elapses, the background expiration engine automatically cancels the reservation and restores the reserved quota to the pool.

---

#### F. Idempotency Engine & Single-Flight Coalescing

Guarantees exactly-once execution for non-idempotent operations (such as payment processing and order creation):

1. **Atomic Claim (`GK.IDEM_BEGIN` / `POST /v1/idempotency/begin`)**:
   - `EXECUTE`: The client is the primary owner and should process the work.
   - `PARK`: A duplicate request is already in progress. The connection is parked in the `ParkingLot` awaiting the result.
   - `REPLAY`: The request previously completed. GateKeeper returns the cached response immediately.
   - `CONFLICT`: Same idempotency key used with a different request payload hash.
2. **Complete (`GK.IDEM_COMPLETE` / `POST /v1/idempotency/complete`)**:
   Saves the response code and body, wakes up all parked waiters, and caches the result.
3. **Fail (`GK.IDEM_FAIL` / `POST /v1/idempotency/fail`)**:
   Marks the execution as failed and wakes up parked waiters.
4. **Lookup (`GK.IDEM_GET <key>`)**:
   Retrieves idempotency status and cached response.

---

#### G. Pluggable AOF Persistence & Crash Recovery

GateKeeper supports an Append-Only File (AOF) persistence engine for durability:

- **Commands Logged**: State-mutating commands (`SET`, `DEL`, `PEXPIRE`, `GK.QUOTA_INIT`, `GK.RESERVE`, `GK.COMMIT`, `GK.ROLLBACK`, `GK.IDEM_BEGIN`, `GK.IDEM_COMPLETE`, `GK.IDEM_FAIL`).
- **Configurable Fsync**:
  - `always`: Fsync on every write (highest durability, lower throughput).
  - `everysec`: Background fsync every second (balanced performance and durability).
  - `no`: Relies on OS filesystem cache flushing.
- **Crash Recovery**: Automatically reconstructs in-memory state on startup, safely ignoring truncated commands caused by power failures.

---

### 4. Installation & Build

#### Prerequisites

- Linux operating system (Kernel 5.4+ recommended for epoll)
- C++20 compliant compiler: GCC 11+ or Clang 13+
- CMake 3.20 or newer
- Python 3.8+ (required for executing integration test suites)
- Optional: Go 1.22+ and Node.js 18+ for SDK tests

#### Building from Source

```bash
git clone https://github.com/ItsK4tune/GateKeeper.git
cd GateKeeper

# Configure and compile Release build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Execute automated test suite (26/26 test targets)
ctest --test-dir build --output-on-failure
```

#### Running the Server Daemon

```bash
# Listen on GKWP port 63779, HTTP port 8080, with AOF persistence enabled
./build/gatekeeper --port 63779 --http-port 8080 --persistence aof --data-dir ./data --fsync everysec --timer 50 --log terminal
```

Server startup flags:
- `-p, --port <port>`: Binary GKWP listening port (default: `63779`).
- `--http-port <port>`: HTTP REST API listening port (default: `0` / disabled).
- `-t, --timer <ms>`: Periodic expiration sweep interval in milliseconds (`-1` to disable).
- `-l, --log <mode>`: Log mode (`none`, `terminal`, `file`).
- `-d, --log-dir <dir>`: Directory where log file is stored.
- `--persistence <mode>`: Persistence mode (`none` or `aof`, default: `none`).
- `--data-dir <dir>`: Directory for persistence files (default: `./data`).
- `--fsync <policy>`: Fsync policy for AOF (`always`, `everysec`, `no`, default: `everysec`).

#### Using the Interactive CLI Client (`gate`)

```bash
# Connect to local GateKeeper server
./build/gate -p 63779

# Inside REPL:
gatekeeper> SET session:token "xyz987" EX 300
OK
gatekeeper> GET session:token
"xyz987"
gatekeeper> GK.RATE_LIMIT user:10 5 60000
allowed=1 remaining=4 retry_after_ms=0
```

---

### 5. Client SDKs and Middlewares

Official client libraries are provided for Go and Node.js/NestJS. Both SDKs use the high-performance **GKWP TCP protocol by default**, while maintaining full support for HTTP fallback.

#### Go SDK (`sdk/go/`)

```go
package main

import (
    "context"
    "fmt"
    "github.com/gatekeeper-kv/gatekeeper/sdk/go"
)

func main() {
    // Default GKWP TCP client (127.0.0.1:63779)
    client := gatekeeper.NewClient("127.0.0.1:63779")
    defer client.Close()

    // Or HTTP client fallback:
    // client := gatekeeper.NewClient("http://127.0.0.1:8080", gatekeeper.WithHTTP())

    ctx := context.Background()
    resp, err := client.CheckRateLimit(ctx, gatekeeper.RateLimitRequest{
        Key:      "user:1001",
        Limit:    10,
        WindowMs: 60000,
    })
    if err == nil && resp.Allowed {
        fmt.Println("Allowed! Remaining:", resp.Remaining)
    }
}
```

- **HTTP & Gin Middlewares**: Built-in `RateLimitMiddleware` and `IdempotencyMiddleware` supporting automatic replay caching and header parsing.

#### Node.js & NestJS SDK (`sdk/nodejs/`)

##### Pure Node.js / Express

```javascript
const { GateKeeperClient, createRateLimitMiddleware, createIdempotencyMiddleware } = require('@gatekeeper-kv/client');

// Default GKWP TCP client
const client = new GateKeeperClient({ host: '127.0.0.1', port: 63779 });

// Or HTTP fallback:
// const client = new GateKeeperClient({ endpoint: 'http://127.0.0.1:8080' });

const app = express();
app.use(createRateLimitMiddleware(client, { limit: 100, windowMs: 60000 }));
app.use(createIdempotencyMiddleware(client));
```

##### NestJS Dynamic Module

```typescript
import { Module } from '@nestjs/common';
import { GateKeeperModule, GateKeeperService } from '@gatekeeper-kv/client';

@Module({
  imports: [
    GateKeeperModule.forRoot({
      host: '127.0.0.1',
      port: 63779, // GKWP TCP default
    }),
    // Or async configuration:
    // GateKeeperModule.forRootAsync({
    //   useFactory: (config: ConfigService) => ({
    //     host: config.get('GATEKEEPER_HOST'),
    //     port: config.get('GATEKEEPER_PORT'),
    //   }),
    //   inject: [ConfigService],
    // }),
  ],
})
export class AppModule {}
```

Inject and use `GateKeeperService`:

```typescript
@Injectable()
export class OrderService {
  constructor(private readonly gkService: GateKeeperService) {}

  async createOrder(userId: string) {
    const rl = await this.gkService.checkRateLimit({
      key: `order:${userId}`,
      limit: 5,
      windowMs: 60000,
    });
    // Process order...
  }
}
```

---

## Tài Liệu Tiếng Việt

### 1. Giới Thiệu & Kiến Trúc Hệ Thống

GateKeeper là hệ thống lưu trữ dữ liệu in-memory, engine kiểm soát tốc độ (rate limiting) phân tán, và xử lý idempotency hiệu năng cao được phát triển bằng ngôn ngữ C++20 hiện đại. Hệ thống được thiết kế chuyên biệt cho kiến trúc vi dịch vụ (microservices), API Gateway, và các pipeline xử lý Generative AI đòi hỏi độ trễ microsecond và tính nhất quán tuyệt đối.

Các đặc điểm kiến trúc cốt lõi:
- **Event Loop đơn luồng non-blocking dựa trên epoll**: Sử dụng cơ chế epoll multiplexing của Linux trên một luồng duy nhất để xử lý I/O bất đồng bộ. Toàn bộ dữ liệu lưu trữ trên RAM được cập nhật tuần tự, loại bỏ hoàn toàn hiện tượng tranh chấp khóa (mutex lock contention) và chi phí chuyển đổi ngữ cảnh (context switching) trên luồng thực thi dữ liệu.
- **Không phụ thuộc thư viện ngoài (Zero External Dependencies)**: Được phát triển hoàn toàn bằng C++20 chuẩn mực, tích hợp sẵn bộ phân tích cú pháp JSON dạng luồng (`protocol::JsonReader`) và bảng băm nội bộ.
- **Hỗ trợ đồng thời hai giao thức**: Lắng nghe song song trên hai cổng mạng độc lập:
  1. Giao thức nhị phân GKWP/1 trên cổng 63779.
  2. Giao thức RESTful HTTP/1.1 trên cổng 8080.
- **Mô hình hết hạn kết hợp (Hybrid Expiration)**: Kết hợp giữa dọn dẹp thụ động (lazy eviction khi có truy cập) và quét ngẫu nhiên chủ động (active eviction định kỳ qua timer tích hợp trong Event Loop).
- **Thuật toán Sliding Window Counter - Hybrid**: Triệt tiêu hiện tượng dồn tải tại ranh giới cửa sổ (Boundary Burst) với độ phức tạp bộ nhớ $O(1)$ và CPU $O(1)$.
- **Động cơ Idempotency & Gom Nhóm Request (Single-Flight)**: Đảm bảo xử lý đúng một lần (exactly-once), tự động gom các request trùng lặp đồng thời vào hàng đợi chờ kết quả (`ParkingLot`).
- **Lưu trữ bền vững AOF có thể cấu hình**: Ghi nhật ký thao tác tuần tự (Append-Only File) với các chế độ fsync linh hoạt và tự động phục hồi sau sự cố.

---

### 2. Đặc Tả Giao Thức GKWP/1

Giao thức GateKeeper Wire Protocol (phiên bản 1) hoạt động trên nền TCP, sử dụng phần đầu cố định 4 byte để xác định độ dài gói tin, phía sau là nội dung JSON định dạng UTF-8.

#### Cấu Trúc Gói Tin

```
+-----------------------------+------------------------------------+
| Length (4 Bytes, Big-Endian)| Payload (N Bytes, UTF-8 JSON)      |
+-----------------------------+------------------------------------+
```

- **Trường Length**: Số nguyên không dấu 32-bit (Big-Endian) xác định chính xác số byte payload tiếp theo (tối đa 16 MiB).
- **Trường Payload**: Đối tượng JSON định dạng UTF-8 chứa nội dung request hoặc response.

---

### 3. Tính Năng & Hướng Dẫn Sử Dụng

#### A. Thao Tác Key-Value Cốt Lõi

##### Lệnh GKWP

| Lệnh | Cú pháp | Mô tả |
| :--- | :--- | :--- |
| `PING` | `PING [message]` | Kiểm tra kết nối liveness. Trả về `PONG` hoặc nội dung message. |
| `SET` | `SET <key> <val> [EX s \| PX ms] [NX \| XX]` | Lưu trữ giá trị chuỗi kèm thời gian sống và điều kiện. |
| `GET` | `GET <key>` | Lấy giá trị của key (trả về null nếu không tồn tại hoặc đã hết hạn). |
| `DEL` | `DEL <key> [key ...]` | Xóa một hoặc nhiều key khỏi hệ thống. |
| `EXISTS` | `EXISTS <key>` | Kiểm tra key có tồn tại và còn hạn hay không. |
| `TYPE` | `TYPE <key>` | Trả về kiểu dữ liệu (`string`, `none`). |
| `DBSIZE` | `DBSIZE` | Lấy tổng số lượng key đang hoạt động trong cơ sở dữ liệu. |
| `KEYS` | `KEYS [pattern]` | Tìm kiếm key theo mẫu pattern. |
| `SCAN` | `SCAN <cursor> [COUNT so_luong]` | Duyệt danh sách key tuần tự bằng con trỏ cursor. |

##### REST HTTP Endpoints

| Method | Path | Body / Query | Mô tả |
| :--- | :--- | :--- | :--- |
| `POST` | `/v1/kv/set` | `{"key":"k","value":"v","ttl_ms":60000}` | Lưu key với TTL tùy chọn (`ttl_ms` hoặc `ttl_seconds`). |
| `GET` | `/v1/kv/get` | `?key=k` | Lấy giá trị của key (trả về 404 nếu không tìm thấy). |
| `POST` | `/v1/kv/del` | `{"key":"k"}` | Xóa key. Trả về `{"deleted": true/false}`. |
| `POST` | `/v1/kv/exists` | `{"key":"k"}` | Kiểm tra tồn tại. Trả về `{"exists": true/false}`. |
| `GET` | `/v1/kv/type` | `?key=k` | Lấy kiểu dữ liệu `{"type": "string"|"none"}`. |

---

#### B. Nhóm Lệnh TTL và Hết Hạn

##### Lệnh GKWP

| Lệnh | Cú pháp | Mô tả |
| :--- | :--- | :--- |
| `EXPIRE` | `EXPIRE <key> <giay>` | Đặt thời gian tồn tại cho key theo giây. |
| `PEXPIRE`| `PEXPIRE <key> <ms>` | Đặt thời gian tồn tại cho key theo mili-giây. |
| `TTL` | `TTL <key>` | Lấy thời gian sống còn lại theo giây (-1 nếu vĩnh viễn, -2 nếu không tồn tại). |
| `PTTL` | `PTTL <key>` | Lấy thời gian sống còn lại theo mili-giây. |
| `PERSIST`| `PERSIST <key>` | Xóa bỏ TTL, chuyển key sang trạng thái vĩnh viễn. |

##### REST HTTP Endpoints

| Method | Path | Body / Query | Mô tả |
| :--- | :--- | :--- | :--- |
| `POST` | `/v1/kv/expire` | `{"key":"k","ttl_seconds":60}` | Thiết lập TTL theo giây hoặc mili-giây (`ttl_ms`). |
| `GET` | `/v1/kv/ttl` | `?key=k` | Lấy TTL còn lại `{"ttl": 60}` (-1 hoặc -2). |

---

#### C. Bộ Đếm Nguyên Tử (Atomic Counters)

- `INCR <key>`: Tăng giá trị nguyên của key lên 1 đơn vị.
- `DECR <key>`: Giảm giá trị nguyên của key đi 1 đơn vị.
- `INCRBY <key> <delta>`: Tăng hoặc giảm giá trị nguyên theo số nguyên có dấu 64-bit một cách nguyên tử.

---

#### D. Kiểm Soát Tốc Độ Cửa Sổ Trượt - Hybrid

GateKeeper triển khai thuật toán **Sliding Window Counter - Hybrid**, kết hợp trọng số giữa cửa sổ trước và cửa sổ hiện tại:

$$\text{weight} = \frac{\text{window\_ms} - (\text{now\_ms} \bmod \text{window\_ms})}{\text{window\_ms}}$$
$$\text{estimated\_count} = \text{previous\_count} \times \text{weight} + \text{current\_count}$$

- **Triệt tiêu Boundary Burst**: Làm mượt lưu lượng tại thời điểm giao thoa giữa hai cửa sổ.
- **Tiết kiệm tài nguyên $O(1)$**: Chỉ cần lưu 2 giá trị bộ đếm cho mỗi key, không gây tốn RAM như Sliding Window Log.

##### Lệnh GKWP

`GK.RATE_LIMIT <key> <limit> <window_ms> [cost]`

##### REST HTTP Endpoint

`POST /v1/rate-limit/check`

```bash
curl -i -X POST http://127.0.0.1:8080/v1/rate-limit/check \
  -H "Content-Type: application/json" \
  -d '{
    "tenant": "default",
    "subject": "192.168.1.100",
    "resource": "api:orders",
    "limit": 60,
    "window_ms": 60000,
    "cost": 1
  }'
```

Tự động trả về các HTTP Header tiêu chuẩn RFC:
- `X-RateLimit-Limit`: Hạn mức tối đa trong một cửa sổ.
- `X-RateLimit-Remaining`: Số lượt request còn lại khả dụng.
- `X-RateLimit-Reset`: Thời điểm cửa sổ reset tính theo Unix timestamp (giây).
- `Retry-After`: Số giây client cần tạm dừng trước khi gửi yêu cầu tiếp theo (trả về khi gặp mã 429).

---

#### E. Cơ Chế Giữ Chỗ Hạn Ngạch 2 Pha (Two-Phase Quota Reservation)

Chuyên dụng cho các quy trình không thể dự đoán chính xác lượng tài nguyên tiêu thụ trước khi thực hiện (như tạo văn bản với LLM, xử lý luồng AI, hoặc thanh toán nhiều bước):

1. **Khởi tạo Quota Pool**:
   `GK.QUOTA_INIT <key> <quota> [ttl_ms]` hoặc `POST /v1/quota/init`
2. **Pha 1 (Reserve - Giữ chỗ)**:
   `GK.RESERVE <key> <amount> <ttl_ms>` hoặc `POST /v1/quota/reserve`
   Trừ trước hạn mức trần dự kiến và cấp phát một `reservation_id` duy nhất.
3. **Pha 2 (Commit hoặc Rollback)**:
   - `GK.COMMIT <key> <reservation_id> [actual_amount]` hoặc `POST /v1/quota/commit`
     Chốt số lượng tiêu thụ thực tế. Nếu `actual_amount < reserved_amount`, lượng hạn mức dư thừa sẽ được hoàn trả ngay lập tức về kho.
   - `GK.ROLLBACK <key> <reservation_id>` hoặc `POST /v1/quota/rollback`
     Hủy bỏ yêu cầu giữ chỗ và hoàn trả 100% quota đã khóa về kho.
4. **Tự Động Hoàn Trả Khi Timeout (Auto-Rollback on Timeout)**:
   Nếu worker bị ngắt kết nối đột ngột hoặc gặp sự cố và không gửi lệnh commit trước khi `ttl_ms` kết thúc, tiến trình quét dọn chủ động sẽ tự động hủy reservation và hoàn trả toàn bộ quota đã giữ lại về kho.

---

#### F. Động Cơ Xử Lý Idempotency & Gom Nhóm Request (Single-Flight)

Đảm bảo tính thực thi đúng một lần (exactly-once) cho các nghiệp vụ thanh toán, tạo đơn hàng:

1. **Atomic Claim (`GK.IDEM_BEGIN` / `POST /v1/idempotency/begin`)**:
   - `EXECUTE`: Client là luồng xử lý đầu tiên, tiến hành gọi xử lý nghiệp vụ.
   - `PARK`: Request trùng lặp đang được xử lý đồng thời bởi worker khác. Kết nối được giữ lại trong `ParkingLot` chờ kết quả.
   - `REPLAY`: Request đã được xử lý xong trước đó. GateKeeper trả về ngay kết quả đã lưu trong cache.
   - `CONFLICT`: Trùng idempotency key nhưng mã băm dữ liệu request (hash) khác nhau.
2. **Complete (`GK.IDEM_COMPLETE` / `POST /v1/idempotency/complete`)**:
   Lưu kết quả HTTP status và body, đồng thời phát sóng (broadcast) kết quả cho toàn bộ kết nối đang chờ trong `ParkingLot`.
3. **Fail (`GK.IDEM_FAIL` / `POST /v1/idempotency/fail`)**:
   Ghi nhận trạng thái thất bại và đánh thức các kết nối đang chờ.
4. **Tra cứu (`GK.IDEM_GET <key>`)**:
   Lấy trạng thái idempotency và kết quả lưu tạm của key.

---

#### G. Lưu Trữ Bền Vững AOF & Phục Hồi Sau Sự Cố

GateKeeper tích hợp engine ghi log thay đổi trạng thái (Append-Only File):

- **Các lệnh được ghi nhật ký**: Các lệnh thay đổi dữ liệu (`SET`, `DEL`, `PEXPIRE`, `GK.QUOTA_INIT`, `GK.RESERVE`, `GK.COMMIT`, `GK.ROLLBACK`, `GK.IDEM_BEGIN`, `GK.IDEM_COMPLETE`, `GK.IDEM_FAIL`).
- **Chế độ fsync**:
  - `always`: Đồng bộ ổ cứng sau mỗi lệnh (độ an toàn cao nhất).
  - `everysec`: Đồng bộ định kỳ mỗi giây (cân bằng giữa tốc độ và độ an toàn).
  - `no`: Dựa vào cơ chế xả cache tự nhiên của hệ điều hành.
- **Phục hồi tự động**: Tự động đọc lại file AOF khi khởi động, khôi phục toàn bộ trạng thái dữ liệu và tự xử lý các lệnh bị đứt đoạn do sự cố mất nguồn đột ngột.

---

### 4. Cài Đặt & Biên Dịch

#### Yêu Cầu Môi Trường

- Hệ điều hành Linux (khuyến nghị Kernel 5.4 trở lên để tối ưu epoll)
- Trình biên dịch C++20: GCC 11+ hoặc Clang 13+
- CMake phiên bản 3.20 trở lên
- Python 3.8+ (dùng để chạy các bài kiểm thử tích hợp)
- Tùy chọn: Go 1.22+ và Node.js 18+ để kiểm thử SDK

#### Các Bước Biên Dịch

```bash
git clone https://github.com/ItsK4tune/GateKeeper.git
cd GateKeeper

# Cấu hình và biên dịch bản Release
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Chạy kiểm thử tự động toàn diện (26/26 test target)
ctest --test-dir build --output-on-failure
```

#### Khởi Chạy Máy Chủ

```bash
# Lắng nghe cổng nhị phân 63779, cổng HTTP 8080, kích hoạt AOF persistence
./build/gatekeeper --port 63779 --http-port 8080 --persistence aof --data-dir ./data --fsync everysec --timer 50 --log terminal
```

Các tham số dòng lệnh:
- `-p, --port <port>`: Cổng lắng nghe giao thức nhị phân GKWP (mặc định: `63779`).
- `--http-port <port>`: Cổng lắng nghe HTTP REST API (mặc định: `0`, tức tắt).
- `-t, --timer <ms>`: Chu kỳ mili-giây quét dọn dẹp key hết hạn (`-1` để tắt).
- `-l, --log <mode>`: Chế độ ghi log (`none`, `terminal`, `file`).
- `-d, --log-dir <dir>`: Thư mục chứa file log khi dùng chế độ file.
- `--persistence <mode>`: Chế độ lưu trữ bền vững (`none` hoặc `aof`, mặc định: `none`).
- `--data-dir <dir>`: Thư mục chứa file dữ liệu AOF (mặc định: `./data`).
- `--fsync <policy>`: Chính sách đồng bộ ổ cứng cho AOF (`always`, `everysec`, `no`, mặc định: `everysec`).

#### Sử Dụng Trình Điều Khiển Dòng Lệnh (`gate`)

```bash
# Chạy chế độ REPL tương tác
./build/gate -p 63779

# Thực thi lệnh trực tiếp
./build/gate -p 63779 PING
./build/gate -p 63779 SET token "sample_value" EX 300
./build/gate -p 63779 GK.RATE_LIMIT client_ip 10 60000
```

---

### 5. Hướng Dẫn Sử Dụng SDK và Middleware

GateKeeper cung cấp thư viện SDK chính thức cho cả Go và Node.js/NestJS. Cả hai SDK đều sử dụng giao thức nhị phân **GKWP TCP làm mặc định** nhằm tối ưu độ trễ, đồng thời hỗ trợ chế độ HTTP fallback.

#### Go SDK (`sdk/go/`)

```go
package main

import (
    "context"
    "fmt"
    "github.com/gatekeeper-kv/gatekeeper/sdk/go"
)

func main() {
    // Mặc định kết nối TCP GKWP (127.0.0.1:63779)
    client := gatekeeper.NewClient("127.0.0.1:63779")
    defer client.Close()

    // Hoặc kết nối qua HTTP fallback:
    // client := gatekeeper.NewClient("http://127.0.0.1:8080", gatekeeper.WithHTTP())

    ctx := context.Background()
    resp, err := client.CheckRateLimit(ctx, gatekeeper.RateLimitRequest{
        Key:      "user:1001",
        Limit:    10,
        WindowMs: 60000,
    })
    if err == nil && resp.Allowed {
        fmt.Println("Allowed! Remaining:", resp.Remaining)
    }
}
```

- Tích hợp sẵn `RateLimitMiddleware` và `IdempotencyMiddleware` cho `net/http` và framework Gin.

#### Node.js & NestJS SDK (`sdk/nodejs/`)

##### Ứng dụng Node.js thuần / Express

```javascript
const { GateKeeperClient, createRateLimitMiddleware, createIdempotencyMiddleware } = require('@gatekeeper-kv/client');

// Mặc định kết nối qua TCP GKWP
const client = new GateKeeperClient({ host: '127.0.0.1', port: 63779 });

// Hoặc kết nối HTTP fallback:
// const client = new GateKeeperClient({ endpoint: 'http://127.0.0.1:8080' });

const app = express();
app.use(createRateLimitMiddleware(client, { limit: 100, windowMs: 60000 }));
app.use(createIdempotencyMiddleware(client));
```

##### Tích hợp NestJS Dynamic Module

```typescript
import { Module } from '@nestjs/common';
import { GateKeeperModule, GateKeeperService } from '@gatekeeper-kv/client';

@Module({
  imports: [
    GateKeeperModule.forRoot({
      host: '127.0.0.1',
      port: 63779, // Mặc định GKWP TCP
    }),
    // Hoặc cấu hình bất đồng bộ (async):
    // GateKeeperModule.forRootAsync({
    //   useFactory: (config: ConfigService) => ({
    //     host: config.get('GATEKEEPER_HOST'),
    //     port: config.get('GATEKEEPER_PORT'),
    //   }),
    //   inject: [ConfigService],
    // }),
  ],
})
export class AppModule {}
```

Inject và sử dụng `GateKeeperService`:

```typescript
@Injectable()
export class OrderService {
  constructor(private readonly gkService: GateKeeperService) {}

  async createOrder(userId: string) {
    const rl = await this.gkService.checkRateLimit({
      key: `order:${userId}`,
      limit: 5,
      windowMs: 60000,
    });
    // Xử lý logic đơn hàng...
  }
}
```
