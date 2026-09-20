# GateKeeper

GateKeeper is a high-performance in-memory key-value store, distributed rate-limiting, and idempotency engine written in C++20. It provides sub-millisecond request validation, atomic counters, two-phase quota reservation with automatic rollback on timeout, an idempotency engine with single-flight request coalescing, pluggable AOF persistence, and dual-protocol connectivity (custom binary TCP protocol GKWP/1 and native HTTP/1.1 REST).

---

## Table of Contents / Mục Lục

- [English Documentation](#english-documentation)
  - [1. Overview & Architecture](#1-overview--architecture)
  - [2. GateKeeper Wire Protocol Specification (GKWP/2 & GKWP/1)](#2-gatekeeper-wire-protocol-specification-gkwp2--gkwp1)
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
  - [2. Đặc Tả Giao Thức GKWP/2 & GKWP/1](#2-đặc-tả-giao-thức-gkwp2--gkwp1)
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
  1. The custom binary GateKeeper Wire Protocol (GKWP/2) on port 63779.
  2. Native HTTP/1.1 REST API on port 8080.
- **Hybrid Expiration Model**: Combines lazy eviction (evaluated upon access) with active periodic expiration (random key sampling triggered by an integrated timer on the event loop).
- **Hybrid Sliding Window Counter**: Rate limiting that eliminates boundary burst while maintaining $O(1)$ memory and $O(1)$ CPU overhead.
- **Idempotency Engine & Single-Flight Coalescing**: Guarantees exactly-once execution for state-mutating requests, automatically deduplicating concurrent duplicate requests via connection parking.
- **Pluggable AOF Persistence**: Write-ahead append-only log (AOF) with configurable fsync policies (`always`, `everysec`, `no`) and automatic crash recovery on startup.

---

### 2. GateKeeper Wire Protocol Specification (GKWP/2 & GKWP/1)

GateKeeper provides two generations of its native binary TCP wire protocol:
- **GKWP/2 (Current Standard)**: High-performance, strictly-aligned 24-byte binary framed protocol designed for sub-millisecond serialization, out-of-order multiplexing, and zero-copy dispatch.
- **GKWP/1 [DEPRECATED]**: Legacy 4-byte length-prefixed JSON protocol maintained for backward compatibility.

---

#### GKWP/2 Specification (Current Default)

GKWP/2 is engineered from the ground up to eliminate JSON parsing overhead, string allocations, and framing ambiguity while retaining structured message typing and multiplexing capabilities.

##### Binary Frame Layout

Every transmission across a GKWP/2 connection begins with a fixed 24-byte header in network byte order (big-endian), followed by optional metadata headers and the payload body:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Magic (0x474B)        |    Version    |    MsgType    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Flags     |     Opcode    |     Status    |    Reserved   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 Sequence / Correlation ID                     |
|                            (64-bit)                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Header Length                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Payload Length                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Header Block (Optional)                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Payload Body (N Bytes)...                 |
+---------------------------------------------------------------+
```

##### Field Definitions

| Field | Size | Type | Description |
| :--- | :--- | :--- | :--- |
| **Magic** | 2 Bytes | `uint16_t` | Constant magic number `0x474B` (ASCII `"GK"`). Used for immediate framing validation. |
| **Version** | 1 Byte | `uint8_t` | Protocol version. Set to `0x02` for GKWP/2. |
| **MsgType** | 1 Byte | `uint8_t` | Message type: `0x01` (Request), `0x02` (Response), `0x03` (Heartbeat/Notification). |
| **Flags** | 1 Byte | `uint8_t` | Bit flags: Bit 0 = Compressed (zstd), Bit 1 = Streaming Chunk, Bit 2 = End of Stream. |
| **Opcode** | 1 Byte | `uint8_t` | Operation code (e.g., `0x01` PING, `0x02` SET, `0x03` GET, `0x0A` RATE_LIMIT). |
| **Status** | 1 Byte | `uint8_t` | Status/Return code: `0x00` (OK), `0x01` (Error), `0x02` (Not Found), `0x03` (Rate Limited). |
| **Reserved** | 1 Byte | `uint8_t` | Reserved for word alignment and future protocol revisions (must be `0x00`). |
| **Sequence ID** | 8 Bytes | `uint64_t` | Client-generated correlation identifier for request/response multiplexing and out-of-order execution. |
| **Header Length**| 4 Bytes | `uint32_t` | Length in bytes of optional metadata headers block (0 if none). |
| **Payload Length**| 4 Bytes | `uint32_t` | Length in bytes of the payload body (up to 16 MiB). |

##### Standard Opcodes

| Opcode | Hex | Command Name | Description |
| :--- | :--- | :--- | :--- |
| 1 | `0x01` | `PING` | Connection health check and round-trip verification |
| 2 | `0x02` | `SET` | Set key-value pair with optional TTL |
| 3 | `0x03` | `GET` | Retrieve value by key |
| 4 | `0x04` | `DEL` | Delete one or more keys |
| 5 | `0x05` | `EXISTS` | Check key existence |
| 6 | `0x06` | `EXPIRE` | Set key expiration time |
| 7 | `0x07` | `TTL` | Retrieve remaining TTL |
| 8 | `0x08` | `INCR` | Increment integer value |
| 9 | `0x09` | `DECR` | Decrement integer value |
| 10 | `0x0A` | `RATE_LIMIT` | Hybrid sliding window rate limit check |
| 11 | `0x0B` | `QUOTA_INIT` | Initialize quota pool |
| 12 | `0x0C` | `RESERVE` | Two-phase quota reservation |
| 13 | `0x0D` | `COMMIT` | Commit reserved quota |
| 14 | `0x0E` | `ROLLBACK` | Rollback reserved quota |
| 15 | `0x0F` | `IDEM_EXEC` | Idempotent execution and single-flight coalescing |
| 16 | `0x10` | `DBSIZE` | Total active database keys |

---

#### Why GKWP/2? (Design Rationale)

1. **Elimination of Text/JSON Serialization Bottlenecks**:
   - In GKWP/1, every request required decoding a JSON envelope (`id`, `op`, `body`), parsing strings, and serializing JSON responses.
   - GKWP/2 utilizes fixed 24-byte binary framing: the server unpacks opcodes, status codes, and sequence IDs in $O(1)$ memory copies without dynamic memory allocations on the fast path.
2. **True Request Multiplexing & Pipelining**:
   - With an explicit 64-bit sequence/correlation ID in the header, clients can issue hundreds of concurrent requests over a single TCP socket without waiting for sequential responses.
   - Responses can be returned in any order if executed asynchronously, eliminating Head-of-Line (HoL) blocking at the application level.
3. **Bandwidth & CPU Cache Efficiency**:
   - The 24-byte header is 64-bit aligned, fitting neatly inside a single CPU cache line (64 bytes).
   - Packet framing overhead drops by over 60% compared to JSON/RESP text headers.
4. **Extensibility & Streaming**:
   - Dedicated `Flags` and `Header Length` fields allow transparent future upgrades such as end-to-end zstd compression, chunked streaming for large objects, and tracing metadata (OpenTelemetry trace IDs) without breaking wire compatibility.

---

#### Benchmark Comparison: GateKeeper vs. Redis 7, Dragonfly & NGINX

Comprehensive benchmarks were conducted on an Ubuntu 22.04 LTS environment (Linux Kernel 6.8.0, 4 vCPU, `somaxconn = 65535`) comparing **GateKeeper** (multi-worker C++20 engine with SO_REUSEPORT, sharded partitioned locks, and GKWP/2 binary TLV protocol) against **Redis 7** (`redis:7-alpine`), **Dragonfly** (`dragonfly:latest`), and **NGINX** (`nginx:alpine` with `limit_req` module):

##### 1. Raw Key-Value Operations (`SET` + `GET`)
> 2,000 requests, 20 concurrent connections.

| Service | Protocol / Architecture | Throughput (ops/s) | p50 (ms) | p95 (ms) | p99 (ms) |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **GateKeeper** | **GKWP/2 Binary TLV (Multi-Worker)** | **9,141.47** | **4.23** | **5.14** | **5.73** |
| **Redis 7** | RESP | 11,096.30 | 3.42 | 4.72 | 5.76 |
| **Dragonfly** | RESP | 11,433.49 | 3.33 | 4.80 | 6.48 |

*GateKeeper delivers ultra-stable tail latency (**5.73ms p99**), matching Redis (5.76ms) and beating Dragonfly (6.48ms).*

##### 2. Distributed Sliding-Window Rate Limiting
> 2,000 requests, 20 concurrent connections (500,000 req / 60s sliding window).

| Service | Implementation Mechanism | Throughput (ops/s) | p50 (ms) | p95 (ms) | p99 (ms) |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **GateKeeper** | **Native C++ Engine (`GK.RATE_LIMIT` Binary)** | **9,111.86** | **2.06** | **2.73** | **2.90** |
| **Redis 7** | Lua Script (`sliding_window.lua`) | 9,099.79 | 2.08 | 2.99 | 3.55 |
| **Dragonfly** | Lua Script (`sliding_window.lua`) | 8,874.06 | 2.07 | 3.36 | 4.21 |

*GateKeeper **outperforms both Redis 7 and Dragonfly** in both throughput and tail latency (**2.90ms p99 vs 3.55ms Redis and 4.21ms Dragonfly**), eliminating the interpretation overhead of the Lua VM.*

##### 3. Two-Phase Quota Reservation (`GK.RESERVE` + `GK.COMMIT`)
> Designed for AI/LLM Token Budgeting & Multi-step Financial Billing (1,000 2-phase cycles, 20 concurrency).

| Service | Operation | Throughput (ops/s) | p50 (ms) | p95 (ms) | p99 (ms) |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **GateKeeper** | `RESERVE` $\rightarrow$ `COMMIT` | **14,168.69** | **2.69** | **3.15** | **3.43** |

*Provides atomic two-phase reservation with sub-3.5ms p99 latency and automatic rollback timers.*

##### 4. Single-Flight Coalescing vs. Redis Spin-Polling (Thundering Herd)
> 50 concurrent requests for the same `Idempotency-Key` within 1ms.

| Metric | GateKeeper (Single-Flight Parking) | Redis (Spin-Polling `while sleep`) | Advantage |
| :--- | :---: | :---: | :--- |
| **Total Processing Time** | **21.99 ms** | 82.58 ms | **GateKeeper is 3.75x faster** |
| **Tail Latency (p99)** | **5.94 ms** | 72.06 ms | **GateKeeper is 12.1x lower** |
| **Network Poll Packets** | **0 packets** (Event-driven broadcast) | **248 packets** | **Zero network overhead** |

*GateKeeper parks concurrent duplicate requests on the TCP socket and broadcasts the result instantly upon completion, completely eliminating thundering herd network storms.*

##### 5. HTTP Rate Limit Surface (`/v1/rate-limit/check`) vs. NGINX `limit_req`
> `wrk` benchmark with 2 threads, 50 connections, 5-second duration.

| Service | Throughput (Requests/sec) | Latency Avg | Latency p50 | Latency p99 | Total Requests / 5s |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **GateKeeper HTTP REST** | **98,289.13** | **638.05 µs** | **243.00 µs** | **5.25 ms** | **491,869** |
| **NGINX `limit_req`** | 29,229.98 | 2.33 ms | 1.44 ms | 15.89 ms | 146,318 |

*GateKeeper's C++20 REST API reaches **~100,000 RPS** with **sub-millisecond average latency (638 µs)**, running **3.36x faster than NGINX**.*

> [!NOTE]
> All benchmark scripts, configurations, and Docker Compose environments are archived and reproducible on branch `bench/service` under `tools/bench/services/`.

---

#### GKWP/1 Specification [DEPRECATED]

> [!WARNING]
> **GKWP/1 is deprecated** starting in GateKeeper v2.0. New client SDKs and services should exclusively use **GKWP/2** or the **HTTP/1.1 REST API**. GKWP/1 documentation is preserved below for reference and legacy integration.

The GateKeeper Wire Protocol (version 1) is a lightweight, bidirectional, length-prefixed TCP protocol designed to minimize parsing overhead while retaining structured JSON payloads for application data.

##### Frame Layout

Every transmission across a GKWP/1 connection consists of a 4-byte header followed by the frame payload:

```
+-----------------------------+------------------------------------+
| Length (4 Bytes, Big-Endian)| Payload (N Bytes, UTF-8 JSON)      |
+-----------------------------+------------------------------------+
```

- **Length Field**: A 32-bit unsigned integer in network byte order (big-endian). It specifies the exact byte length of the trailing payload. The maximum allowed frame length is 16 MiB (16,777,216 bytes).
- **Payload**: A valid UTF-8 JSON object representing either a request or a response.

##### Request Frame Format

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

##### Response Frame Format

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

$$\text{weight} = \frac{W - (T \bmod W)}{W}$$
$$\text{estimated count} = N_{\text{prev}} \times \text{weight} + N_{\text{curr}}$$

Where:
- $W$: Window duration in milliseconds (`window_ms`)
- $T$: Current timestamp in milliseconds (`now_ms`)
- $N_{\text{prev}}$: Request count in previous window
- $N_{\text{curr}}$: Request count in current window

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
  1. Giao thức nhị phân GKWP/2 trên cổng 63779.
  2. Giao thức RESTful HTTP/1.1 trên cổng 8080.
- **Mô hình hết hạn kết hợp (Hybrid Expiration)**: Kết hợp giữa dọn dẹp thụ động (lazy eviction khi có truy cập) và quét ngẫu nhiên chủ động (active eviction định kỳ qua timer tích hợp trong Event Loop).
- **Thuật toán Sliding Window Counter - Hybrid**: Triệt tiêu hiện tượng dồn tải tại ranh giới cửa sổ (Boundary Burst) với độ phức tạp bộ nhớ $O(1)$ và CPU $O(1)$.
- **Động cơ Idempotency & Gom Nhóm Request (Single-Flight)**: Đảm bảo xử lý đúng một lần (exactly-once), tự động gom các request trùng lặp đồng thời vào hàng đợi chờ kết quả (`ParkingLot`).
- **Lưu trữ bền vững AOF có thể cấu hình**: Ghi nhật ký thao tác tuần tự (Append-Only File) với các chế độ fsync linh hoạt và tự động phục hồi sau sự cố.

---

### 2. Đặc Tả Giao Thức GKWP/2 & GKWP/1

GateKeeper hỗ trợ hai thế hệ giao thức nhị phân gốc qua cổng TCP:
- **GKWP/2 (Chuẩn Hiện Tại)**: Giao thức nhị phân hiệu năng cao, căn chỉnh bộ nhớ 24-byte header cố định, tối ưu hóa zero-copy, ghép kênh (multiplexing) bất đồng bộ và giảm thiểu tối đa độ trễ.
- **GKWP/1 [DEPRECATED / KHÔNG KHUYẾN NGHỊ]**: Giao thức tiền thân sử dụng 4-byte tiền tố độ dài kết hợp nội dung JSON UTF-8.

---

#### Đặc Tả GKWP/2 (Chuẩn Mặc Định Hiện Tại)

GKWP/2 được thiết kế lại hoàn toàn nhằm loại bỏ chi phí phân tích chuỗi JSON, cấp phát bộ nhớ động không cần thiết, đồng thời cung cấp khả năng ghép kênh và mở rộng trong tương lai.

##### Cấu Trúc Gói Tin Nhị Phân

Mọi gói tin truyền qua kết nối GKWP/2 đều bắt đầu bằng phần header cố định 24 byte theo chuẩn mạng (big-endian), nối tiếp bởi khối header mở rộng (tùy chọn) và thân dữ liệu (payload):

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Magic (0x474B)        |    Version    |    MsgType    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Flags     |     Opcode    |     Status    |    Reserved   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 Sequence / Correlation ID                     |
|                            (64-bit)                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Header Length                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Payload Length                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Header Block (Tùy chọn)                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Payload Body (N Bytes)...                 |
+---------------------------------------------------------------+
```

##### Định Nghĩa Các Trường

| Trường | Kích thước | Kiểu dữ liệu | Mô tả |
| :--- | :--- | :--- | :--- |
| **Magic** | 2 Bytes | `uint16_t` | Số nhận diện cố định `0x474B` (chuỗi ASCII `"GK"`). Giúp kiểm tra tính toàn vẹn gói tin ngay lập tức. |
| **Version** | 1 Byte | `uint8_t` | Phiên bản giao thức (`0x02` đối với GKWP/2). |
| **MsgType** | 1 Byte | `uint8_t` | Phân loại thông điệp: `0x01` (Request), `0x02` (Response), `0x03` (Heartbeat/Notification). |
| **Flags** | 1 Byte | `uint8_t` | Cờ nhị phân: Bit 0 = Nén dữ liệu, Bit 1 = Streaming Chunk, Bit 2 = Kết thúc luồng (End of Stream). |
| **Opcode** | 1 Byte | `uint8_t` | Mã lệnh thao tác (ví dụ: `0x01` PING, `0x02` SET, `0x03` GET, `0x0A` RATE_LIMIT). |
| **Status** | 1 Byte | `uint8_t` | Trạng thái phản hồi: `0x00` (OK), `0x01` (Error), `0x02` (Not Found), `0x03` (Rate Limited). |
| **Reserved** | 1 Byte | `uint8_t` | Dành riêng cho căn lề bộ nhớ và tương thích tương lai (luôn là `0x00`). |
| **Sequence ID** | 8 Bytes | `uint64_t` | Định danh yêu cầu 64-bit do client tạo để ghép kênh (multiplexing) và xử lý bất đồng bộ. |
| **Header Length**| 4 Bytes | `uint32_t` | Độ dài tính bằng byte của khối header bổ sung (bằng 0 nếu không có). |
| **Payload Length**| 4 Bytes | `uint32_t` | Độ dài tính bằng byte của thân dữ liệu (tối đa 16 MiB). |

##### Danh Mục Opcode Chuẩn

| Opcode | Hex | Tên Lệnh | Mô tả |
| :--- | :--- | :--- | :--- |
| 1 | `0x01` | `PING` | Kiểm tra kết nối liveness và đo thời gian phản hồi |
| 2 | `0x02` | `SET` | Lưu trữ cặp key-value kèm TTL tùy chọn |
| 3 | `0x03` | `GET` | Truy xuất giá trị theo key |
| 4 | `0x04` | `DEL` | Xóa một hoặc nhiều key |
| 5 | `0x05` | `EXISTS` | Kiểm tra sự tồn tại của key |
| 6 | `0x06` | `EXPIRE` | Thiết lập thời gian sống (TTL) cho key |
| 7 | `0x07` | `TTL` | Lấy thời gian sống còn lại của key |
| 8 | `0x08` | `INCR` | Tăng giá trị nguyên của key thêm 1 |
| 9 | `0x09` | `DECR` | Giảm giá trị nguyên của key đi 1 |
| 10 | `0x0A` | `RATE_LIMIT` | Kiểm tra giới hạn tốc độ theo thuật toán Sliding Window Hybrid |
| 11 | `0x0B` | `QUOTA_INIT` | Khởi tạo nhóm hạn ngạch tài nguyên |
| 12 | `0x0C` | `RESERVE` | Giữ trước hạn ngạch (pha 1) |
| 13 | `0x0D` | `COMMIT` | Xác nhận lượng hạn ngạch thực tế tiêu thụ (pha 2) |
| 14 | `0x0E` | `ROLLBACK` | Hủy bỏ giữ chỗ và hoàn trả 100% hạn ngạch |
| 15 | `0x0F` | `IDEM_EXEC` | Thực thi đảm bảo idempotency và gom request trùng lặp |
| 16 | `0x10` | `DBSIZE` | Lấy tổng số lượng key đang có trong cơ sở dữ liệu |

---

#### Tại Sao Lựa Chọn GKWP/2? (Lý Do Lựa Chọn)

1. **Triệt tiêu nghẽn cổ chai phân tích chuỗi / JSON**:
   - Ở GKWP/1, mỗi thao tác đều đòi hỏi parse JSON (`id`, `op`, `body`), cấp phát chuỗi và tuần tự hóa JSON cho phản hồi.
   - GKWP/2 sử dụng cấu trúc nhị phân 24 byte cố định: server trích xuất opcode, status code và sequence ID trong thời gian $O(1)$ mà không cần cấp phát bộ nhớ động trên luồng xử lý chính.
2. **Hỗ trợ Ghép Kênh (Multiplexing) & Pipelining Thực Thụ**:
   - Trường Correlation/Sequence ID 64-bit cho phép một kết nối TCP duy nhất gửi hàng trăm request đồng thời mà không phải chờ đợi phản hồi tuần tự theo thứ tự đến (loại bỏ Head-of-Line blocking ở tầng ứng dụng).
3. **Tối ưu hóa Băng Thông và Bộ Nhớ Cache CPU**:
   - Header 24-byte được căn chỉnh chuẩn 64-bit, nằm trọn vẹn trong một đường cache CPU (64 bytes).
   - Dung lượng header giảm hơn 60% so với định dạng văn bản JSON hay Redis RESP.
4. **Khả năng mở rộng trong tương lai**:
   - Các trường `Flags` và `Header Length` cho phép tích hợp nén luồng zstd, phân mảnh dữ liệu (chunking) cho payload lớn, và gắn kèm metadata truy vết phân tán (OpenTelemetry Trace Context) mà không phá vỡ tính tương thích ngược.

---

#### Bảng So Sánh Hiệu Năng Thực Nghiệm: GateKeeper vs. Redis 7, Dragonfly & NGINX

Bộ benchmark thực nghiệm được thực hiện trên môi trường Ubuntu 22.04 LTS (Linux Kernel 6.8.0, 4 vCPU, `somaxconn = 65535`) so sánh **GateKeeper** (kiến trúc multi-worker C++20 với `SO_REUSEPORT`, sharded storage 64 phân vùng và giao thức nhị phân GKWP/2 Binary TLV) với **Redis 7** (`redis:7-alpine`), **Dragonfly** (`dragonfly:latest`) và **NGINX** (`nginx:alpine` với module `limit_req`):

##### 1. Thao tác Key-Value Cơ bản (`SET` + `GET`)
> 2.000 requests, 20 kết nối đồng thời.

| Dịch vụ | Giao thức / Kiến trúc | Thông lượng (ops/s) | p50 (ms) | p95 (ms) | p99 (ms) |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **GateKeeper** | **GKWP/2 Binary TLV (Multi-Worker)** | **9.141,47** | **4,23** | **5,14** | **5,73** |
| **Redis 7** | RESP | 11.096,30 | 3,42 | 4,72 | 5,76 |
| **Dragonfly** | RESP | 11.433,49 | 3,33 | 4,80 | 6,48 |

*GateKeeper đạt độ trễ đuôi cực kỳ ổn định (**5,73ms p99**), ngang ngửa Redis (5,76ms) và vượt qua Dragonfly (6,48ms).*

##### 2. Distributed Sliding-Window Rate Limiting
> 2.000 requests, 20 kết nối đồng thời (hạn mức 500.000 req / 60s sliding window).

| Dịch vụ | Cơ chế thực thi | Thông lượng (ops/s) | p50 (ms) | p95 (ms) | p99 (ms) |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **GateKeeper** | **Native C++ Engine (`GK.RATE_LIMIT` Binary)** | **9.111,86** | **2,06** | **2,73** | **2,90** |
| **Redis 7** | Lua Script (`sliding_window.lua`) | 9.099,79 | 2,08 | 2,99 | 3,55 |
| **Dragonfly** | Lua Script (`sliding_window.lua`) | 8.874,06 | 2,07 | 3,36 | 4,21 |

*GateKeeper **vượt qua cả Redis 7 và Dragonfly** ở cả thông lượng lẫn độ trễ đuôi (**2,90ms p99 so với 3,55ms của Redis và 4,21ms của Dragonfly**), loại bỏ hoàn toàn chi phí thông dịch của máy ảo Lua VM.*

##### 3. Two-Phase Quota Reservation (`GK.RESERVE` + `GK.COMMIT`)
> Thiết kế chuyên biệt cho AI/LLM Token Budgeting & Multi-step Billing (1.000 chu kỳ 2-phase, 20 kết nối).

| Dịch vụ | Thao tác | Thông lượng (ops/s) | p50 (ms) | p95 (ms) | p99 (ms) |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **GateKeeper** | `RESERVE` $\rightarrow$ `COMMIT` | **14.168,69** | **2,69** | **3,15** | **3,43** |

*Cung cấp cơ chế đặt trước quota 2 pha nguyên tử với độ trễ p99 dưới 3,5ms và bộ đếm thời gian tự động hoàn tiền (auto-rollback).*

##### 4. Single-Flight Coalescing vs. Redis Spin-Polling (Thundering Herd)
> 50 requests đồng thời gửi cùng một `Idempotency-Key` trong cùng 1 mili-giây.

| Chỉ số đo | GateKeeper (Single-Flight Parking) | Redis (Spin-Polling `while sleep`) | Ưu thế |
| :--- | :---: | :---: | :--- |
| **Tổng thời gian xử lý** | **21,99 ms** | 82,58 ms | **GateKeeper nhanh hơn 3,75 lần** |
| **Độ trễ đuôi (p99)** | **5,94 ms** | 72,06 ms | **GateKeeper nhanh hơn 12,1 lần** |
| **Số gói tin thăm dò mạng (Poll Packets)** | **0 gói tin** (Event-driven broadcast) | **248 gói tin** | **Triệt tiêu hoàn toàn nghẽn mạng** |

*GateKeeper neo các kết nối trùng lặp trên socket TCP và phát sóng kết quả ngay khi request đầu hoàn tất, triệt tiêu hoàn toàn cơn bão thăm dò mạng Thundering Herd.*

##### 5. HTTP Rate Limit Surface (`/v1/rate-limit/check`) vs. NGINX `limit_req`
> Đo đạc bằng `wrk` với 2 threads, 50 kết nối, thời gian 5 giây.

| Dịch vụ | Thông lượng (Requests/sec) | Độ trễ Trung bình | Độ trễ p50 | Độ trễ p99 | Tổng requests / 5s |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **GateKeeper HTTP REST** | **98.289,13** | **638,05 µs** | **243,00 µs** | **5,25 ms** | **491.869** |
| **NGINX `limit_req`** | 29.229,98 | 2,33 ms | 1,44 ms | 15,89 ms | 146.318 |

*Cổng REST API HTTP của GateKeeper đạt **gần 100.000 RPS** với **độ trễ trung bình sub-millisecond (638 µs)**, chạy **nhanh hơn 3,36 lần so với NGINX**.*

> [!NOTE]
> Toàn bộ kịch bản kiểm thử, file cấu hình và Docker Compose được lưu trữ và có thể tái lập tại nhánh `bench/service` trong thư mục `tools/bench/services/`.

---

#### Đặc Tả GKWP/1 [DEPRECATED / KHÔNG KHUYẾN NGHỊ]

> [!WARNING]
> **GKWP/1 đã bị đánh dấu deprecated** từ phiên bản GateKeeper v2.0. Các hệ thống và client mới được khuyến nghị sử dụng **GKWP/2** hoặc **REST API HTTP/1.1**. Nội dung dưới đây được lưu giữ nhằm phục vụ việc tích hợp với các hệ thống cũ.

Giao thức GateKeeper Wire Protocol (phiên bản 1) hoạt động trên nền TCP, sử dụng phần đầu cố định 4 byte để xác định độ dài gói tin, phía sau là nội dung JSON định dạng UTF-8.

##### Cấu Trúc Gói Tin

```
+-----------------------------+------------------------------------+
| Length (4 Bytes, Big-Endian)| Payload (N Bytes, UTF-8 JSON)      |
+-----------------------------+------------------------------------+
```

- **Trường Length**: Số nguyên không dấu 32-bit (Big-Endian) xác định chính xác số byte payload tiếp theo (tối đa 16 MiB).
- **Trường Payload**: Đối tượng JSON định dạng UTF-8 chứa nội dung request hoặc response.

##### Định Dạng Gói Tin Yêu Cầu (Request)

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

##### Định Dạng Gói Tin Phản Hồi (Response)

Thành công:
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

Thất bại:
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

$$\text{weight} = \frac{W - (T \bmod W)}{W}$$
$$\text{estimated count} = N_{\text{prev}} \times \text{weight} + N_{\text{curr}}$$

Trong đó:
- $W$: Độ dài cửa sổ tính theo mili-giây (`window_ms`)
- $T$: Thời điểm hiện tại tính theo mili-giây (`now_ms`)
- $N_{\text{prev}}$: Số lượng request của cửa sổ trước
- $N_{\text{curr}}$: Số lượng request của cửa sổ hiện tại

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
