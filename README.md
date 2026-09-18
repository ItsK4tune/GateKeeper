# GateKeeper

GateKeeper is a high-performance in-memory key-value store and distributed rate-limiting engine written in C++20. It provides sub-millisecond request validation, atomic counters, a two-phase quota reservation system with automatic rollback on timeout, and dual-protocol connectivity (custom binary TCP and native HTTP/1.1 REST).

---

## Table of Contents / Mục Lục

- [English Documentation](#english-documentation)
  - [1. Overview & Architecture](#1-overview--architecture)
  - [2. GateKeeper Wire Protocol Specification (GKWP/1)](#2-gatekeeper-wire-protocol-specification-gkwp1)
  - [3. Features & Command Reference](#3-features--command-reference)
  - [4. Installation & Build](#4-installation--build)
  - [5. Writing Client SDKs and Middlewares](#5-writing-client-sdks-and-middlewares)
- [Tài Liệu Tiếng Việt](#tài-liệu-tiếng-việt)
  - [1. Giới Thiệu & Kiến Trúc Hệ Thống](#1-giới-thiệu--kiến-trúc-hệ-thống)
  - [2. Đặc Tả Giao Thức GKWP/1](#2-đặc-tả-giao-thức-gkwp1)
  - [3. Tính Năng & Hướng Dẫn Sử Dụng](#3-tính-năng--hướng-dẫn-sử-dụng)
  - [4. Cài Đặt & Biên Dịch](#4-cài-đặt--biên-dịch)
  - [5. Hướng Dẫn Xây Dựng SDK và Middleware](#5-hướng-dẫn-xây-dựng-sdk-và-middleware)

---

## English Documentation

### 1. Overview & Architecture

GateKeeper is designed for infrastructure architectures that require centralized, highly consistent rate limiting and resource quota accounting across distributed microservices.

Core architectural components:

- **Single-Threaded Non-Blocking Event Loop**: Built on Linux epoll multiplexing with edge/level-triggered socket handling and partial read/write buffering. The entire dataset and counter operations reside in memory, executed sequentially without thread context-switching or mutex contention in the execution path.
- **Zero External Dependencies**: The server is implemented entirely using the modern C++20 standard library (conforming to strict memory safety and RAII), containing a custom RFC 8259 JSON parser (`protocol::JsonReader`) and an internal hash table engine.
- **Dual-Protocol Listener**: The event loop simultaneously services incoming connections on two independent ports:
  1. The custom binary length-prefixed GateKeeper Wire Protocol (GKWP/1) on port 63779.
  2. Native HTTP/1.1 REST API on port 8080.
- **Hybrid Expiration Model**: Combines lazy eviction (evaluated upon access) with active periodic expiration (random key sampling triggered by an integrated timer on the event loop).

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
  "status": "ok",
  "data": {
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
  "status": "error",
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

| Command | Syntax | Description |
| :--- | :--- | :--- |
| `PING` | `PING [message]` | Tests connection liveness. Returns `PONG` or echoed message. |
| `SET` | `SET <key> <value> [EX seconds \| PX ms] [NX \| XX]` | Sets key to string value with optional TTL and conditions. |
| `GET` | `GET <key>` | Retrieves the value of key. Returns null if not found or expired. |
| `DEL` | `DEL <key> [key ...]` | Removes specified keys from the database. |
| `EXISTS` | `EXISTS <key>` | Checks if key exists and is unexpired. |
| `TYPE` | `TYPE <key>` | Returns the data type (`string`, `none`). |
| `DBSIZE` | `DBSIZE` | Returns total number of active entries in the database. |
| `KEYS` | `KEYS <pattern>` | Returns keys matching glob pattern (supports `*`, `?`). |
| `SCAN` | `SCAN <cursor> [COUNT count]` | Iterates database keys progressively. |

#### B. TTL and Eviction Commands

| Command | Syntax | Description |
| :--- | :--- | :--- |
| `EXPIRE` | `EXPIRE <key> <seconds>` | Sets a timeout on key in seconds. |
| `PEXPIRE`| `PEXPIRE <key> <ms>` | Sets a timeout on key in milliseconds. |
| `TTL` | `TTL <key>` | Returns remaining TTL in seconds (-1 if no TTL, -2 if not found). |
| `PTTL` | `PTTL <key>` | Returns remaining TTL in milliseconds (-1 if no TTL, -2 if not found). |
| `PERSIST`| `PERSIST <key>` | Removes existing timeout from a key. |

#### C. Atomic Counter Operations

- `INCR <key>`: Increments integer value by 1.
- `DECR <key>`: Decrements integer value by 1.
- `INCRBY <key> <delta>`: Atomically increments or decrements integer value by signed 64-bit integer.

#### D. Distributed Sliding-Window Rate Limiting

- **GKWP Command**: `GK.RATE_LIMIT <key> <limit> <window_ms> [cost]`
  Evaluates consumption against a sliding window. If current usage plus cost is within limit, increments counter and returns `allowed=1` with remaining quota. Otherwise, returns `allowed=0` with `retry_after_ms`.
- **HTTP REST Endpoint**: `POST /v1/rate-limit/check`
  ```bash
  curl -i -X POST http://127.0.0.1:8080/v1/rate-limit/check     -H "Content-Type: application/json"     -d '{
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
  - `Retry-After`: Seconds to wait before retrying (sent when status is 429).

#### E. Two-Phase Quota Reservation (GenAI & Distributed Billing)

Designed for multi-step workflows where token or credit consumption cannot be predicted upfront:

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
   If an external worker crashes or fails to commit before `ttl_ms` elapses, the background expiration engine automatically cancels the reservation and restores the reserved quota to the pool.

---

### 4. Installation & Build

#### Prerequisites

- Linux operating system (Kernel 5.4+ recommended for epoll)
- C++20 compliant compiler: GCC 11+ or Clang 13+
- CMake 3.20 or newer
- Python 3.8+ (required for executing integration test suites)
- Optional: Go 1.22+ and Node.js 18+ for compiling SDK tests

#### Building from Source

```bash
git clone https://github.com/ItsK4tune/GateKeeper.git
cd GateKeeper

# Configure and compile Release build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Execute automated test suite (20/20 test targets)
ctest --test-dir build --output-on-failure
```

#### Running the Server Daemon

```bash
# Listen on binary port 63779 and HTTP port 8080 with 50ms active purge timer
./build/gatekeeper --port 63779 --http-port 8080 --timer 50 --log terminal
```

Server startup flags:
- `-p, --port <port>`: Binary GKWP listening port (default: `63779`).
- `--http-port <port>`: HTTP REST API listening port (default: `0` / disabled).
- `-t, --timer <ms>`: Periodic expiration sweep interval in milliseconds (`-1` to disable).
- `-l, --log <mode>`: Log mode (`none`, `terminal`, `file`).
- `-d, --log-dir <dir>`: Directory where log file is stored.

#### Using the Interactive CLI Client

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

### 5. Writing Client SDKs and Middlewares

Developers can integrate GateKeeper into any backend technology stack using either the binary GKWP protocol or the HTTP REST surface.

#### Approach 1: HTTP-Based Middleware (Recommended for Web Frameworks)

Writing a middleware for frameworks such as Go (Gin/Fiber), Node.js (Express/Fastify), Python (FastAPI/Django), or Java (Spring Boot) follows a standard 4-step pipeline:

```
[ Incoming HTTP Request ]
           |
           v
[ Step 1: Extract Subject Key (Client IP, API Key, User ID) ]
           |
           v
[ Step 2: POST to GateKeeper /v1/rate-limit/check ]
           |
     +-----+------------------------+
     |                              |
[ Status: 200 OK ]         [ Status: 429 Too Many Requests ]
     |                              |
     v                              v
Set X-RateLimit Headers     Set X-RateLimit Headers & Retry-After
Pass to next handler        Short-circuit: return 429 JSON response
```

Key considerations for robust middleware implementation:
- **Connection Reuse**: Keep HTTP connections alive using connection pooling (`keep-alive`) to avoid TCP socket setup penalties.
- **Fail-Open vs Fail-Closed**: Decide behavior if GateKeeper is unreachable. For customer-facing APIs, fail-open (`next()`) avoids service outages. For sensitive endpoints (e.g. login brute-force defense), fail-closed (`503 Service Unavailable`) is preferred.
- **Header Propagation**: Always forward `X-RateLimit-Limit`, `X-RateLimit-Remaining`, and `X-RateLimit-Reset` downstream so frontend clients can throttle requests adaptively.

#### Approach 2: GKWP Binary Client (Lowest Latency RPC)

For high-throughput internal RPC communications:
1. Open persistent TCP socket to port `63779`.
2. Construct request JSON: `{"id": id, "op": op, "body": args}`.
3. Prepend 4 bytes containing big-endian length.
4. Send full buffer to socket.
5. Read 4 bytes to determine incoming response payload length, then read payload bytes.

Official implementations are available under `sdk/go/` and `sdk/nodejs/`.

---

## Tài Liệu Tiếng Việt

### 1. Giới Thiệu & Kiến Trúc Hệ Thống

GateKeeper là hệ thống lưu trữ dữ liệu in-memory và engine kiểm soát tốc độ (rate limiting) phân tán, hiệu năng cao được phát triển bằng ngôn ngữ C++20 hiện đại. Hệ thống được thiết kế chuyên biệt cho kiến trúc vi dịch vụ (microservices), API Gateway, và các pipeline xử lý Generative AI đòi hỏi độ trễ microsecond và tính nhất quán tuyệt đối.

Các đặc điểm kiến trúc cốt lõi:
- **Event Loop đơn luồng non-blocking dựa trên epoll**: Sử dụng cơ chế epoll multiplexing của Linux trên một luồng duy nhất để xử lý I/O bất đồng bộ. Toàn bộ dữ liệu lưu trữ trên RAM được cập nhật tuần tự, loại bỏ hoàn toàn hiện tượng tranh chấp khóa (mutex lock contention) và chi phí chuyển đổi ngữ cảnh (context switching) trên luồng thực thi dữ liệu.
- **Không phụ thuộc thư viện ngoài (Zero External Dependencies)**: Được phát triển hoàn toàn bằng C++20 chuẩn mực, tích hợp sẵn bộ phân tích cú pháp JSON dạng luồng (`protocol::JsonReader`) và bảng băm nội bộ.
- **Hỗ trợ đồng thời hai giao thức**: Lắng nghe song song trên hai cổng mạng độc lập:
  1. Giao thức nhị phân GKWP/1 trên cổng 63779.
  2. Giao thức RESTful HTTP/1.1 trên cổng 8080.
- **Mô hình hết hạn kết hợp (Hybrid Expiration)**: Kết hợp giữa dọn dẹp thụ động (lazy eviction khi có truy cập) và quét ngẫu nhiên chủ động (active eviction định kỳ qua timer tích hợp trong Event Loop).

---

### 2. Đặc Tả Giao Thức GKWP/1

Giao thức GateKeeper Wire Protocol (phiên bản 1) hoạt động trên nền TCP, sử dụng phần đầu cố định 4 byte để xác định độ dài gói tin, phía sau là nội dung JSON định dạng UTF-8.

#### Cấu Trúc Gói Tin

```
+------------------------------------+----------------------------------+
| Độ dài Payload (4 Byte Big-Endian) | Dữ liệu JSON (N Byte UTF-8)      |
+------------------------------------+----------------------------------+
```

- **Trường độ dài (4 Byte đầu)**: Số nguyên không dấu 32-bit (Big-Endian uint32) thể hiện chính xác kích thước byte của phần payload. Kích thước tối đa cho phép hiện tại là 16 MiB (16.777.216 byte).
- **Phần Payload**: Chuỗi JSON hợp lệ thể hiện request hoặc response.

#### Định Dạng Request

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

- `id` (chuỗi, bắt buộc): Mã định danh yêu cầu do client tự sinh để khớp nối kết quả.
- `op` (chuỗi, bắt buộc): Tên lệnh thực thi (không phân biệt chữ hoa, chữ thường).
- `body` (object, tùy chọn): Các tham số của lệnh dưới dạng các trường JSON.

#### Định Dạng Response

Phản hồi thành công:
```json
{
  "id": "req-1001",
  "status": "ok",
  "data": {
    "allowed": true,
    "remaining": 99,
    "retry_after_ms": 0
  }
}
```

Phản hồi lỗi:
```json
{
  "id": "req-1001",
  "status": "error",
  "error": {
    "code": "INVALID_ARGUMENTS",
    "message": "limit must be greater than zero"
  }
}
```

Giao thức hỗ trợ kỹ thuật đóng gói nhiều yêu cầu liên tiếp (request pipelining) trên cùng một kết nối TCP duy trì lâu dài.

---

### 3. Tính Năng & Hướng Dẫn Sử Dụng

#### A. Nhóm Lệnh Key-Value Cơ Bản

| Lệnh | Cú pháp | Mô tả |
| :--- | :--- | :--- |
| `PING` | `PING [tin_nhan]` | Kiểm tra kết nối. Trả về `PONG` hoặc nội dung tin nhắn. |
| `SET` | `SET <key> <value> [EX giay \| PX ms] [NX \| XX]` | Lưu giá trị chuỗi, hỗ trợ đặt TTL và điều kiện ghi. |
| `GET` | `GET <key>` | Lấy giá trị của key. Trả về null nếu không tồn tại hoặc đã hết hạn. |
| `DEL` | `DEL <key> [key ...]` | Xóa một hoặc nhiều key khỏi bộ nhớ. |
| `EXISTS` | `EXISTS <key>` | Kiểm tra key có tồn tại và còn hạn hay không. |
| `TYPE` | `TYPE <key>` | Trả về kiểu dữ liệu (`string`, `none`). |
| `DBSIZE` | `DBSIZE` | Trả về tổng số lượng key đang hoạt động trong database. |
| `KEYS` | `KEYS <pattern>` | Tìm kiếm danh sách key theo mẫu glob (`*`, `?`). |
| `SCAN` | `SCAN <cursor> [COUNT so_luong]` | Duyệt danh sách key tuần tự bằng con trỏ cursor. |

#### B. Nhóm Lệnh TTL và Hết Hạn

| Lệnh | Cú pháp | Mô tả |
| :--- | :--- | :--- |
| `EXPIRE` | `EXPIRE <key> <giay>` | Đặt thời gian tồn tại cho key theo giây. |
| `PEXPIRE`| `PEXPIRE <key> <ms>` | Đặt thời gian tồn tại cho key theo mili-giây. |
| `TTL` | `TTL <key>` | Lấy thời gian sống còn lại theo giây (-1 nếu vĩnh viễn, -2 nếu không tồn tại). |
| `PTTL` | `PTTL <key>` | Lấy thời gian sống còn lại theo mili-giây. |
| `PERSIST`| `PERSIST <key>` | Xóa bỏ TTL, chuyển key sang trạng thái vĩnh viễn. |

#### C. Bộ Đếm Nguyên Tử (Atomic Counters)

- `INCR <key>`: Tăng giá trị nguyên của key lên 1 đơn vị.
- `DECR <key>`: Giảm giá trị nguyên của key đi 1 đơn vị.
- `INCRBY <key> <delta>`: Tăng hoặc giảm giá trị nguyên theo số nguyên có dấu 64-bit một cách nguyên tử.

#### D. Kiểm Soát Tốc Độ Cửa Sổ Trượt (Sliding-Window Rate Limiting)

- **Lệnh GKWP**: `GK.RATE_LIMIT <key> <limit> <window_ms> [cost]`
  Kiểm tra và trừ hạn mức trong cửa sổ trượt. Nếu hạn mức còn đủ, cộng dồn số lượng và trả về `allowed=1` kèm `remaining`. Nếu vượt ngưỡng, trả về `allowed=0` kèm thời gian cần chờ `retry_after_ms`.
- **REST HTTP Endpoint**: `POST /v1/rate-limit/check`
  ```bash
  curl -i -X POST http://127.0.0.1:8080/v1/rate-limit/check     -H "Content-Type: application/json"     -d '{
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

#### E. Cơ Chế Giữ Chỗ Hạn Ngạch 2 Pha (Two-Phase Quota Reservation)

Chuyên dụng cho các quy trình không thể dự đoán chính xác lượng tài nguyên tiêu thụ trước khi thực hiện (như tạo văn bản với LLM, xử lý luồng AI, hoặc thanh toán nhiều bước):

1. **Khởi tạo Quota Pool**:
   `GK.QUOTA_INIT <key> <quota> [ttl_ms]` hoặc `POST /v1/quota/init`
2. **Pha 1 (Reserve - Giữ chỗ)**:
   `GK.RESERVE <key> <amount> <ttl_ms>` hoặc `POST /v1/quota/reserve`
   Trừ trước hạn mức trần dự kiến và cấp phát một `reservation_id` duy nhất.
3. **Pha 2 (Commit hoặc Rollback)**:
   - `GK.COMMIT <key> <reservation_id> [actual_amount]` hoặc `POST /v1/quota/commit`
     Chốt số lượng tiêu thụ thực tế. Nếu `actual_amount < reserved_amount`, lượng hạn mức dư thừa sẽ được hoàn trả (refund) ngay lập tức về kho.
   - `GK.ROLLBACK <key> <reservation_id>` hoặc `POST /v1/quota/rollback`
     Hủy bỏ yêu cầu giữ chỗ và hoàn trả 100% quota đã khóa về kho.
4. **Tự Động Hoàn Trả Khi Timeout (Auto-Rollback on Timeout)**:
   Nếu worker bị ngắt kết nối đột ngột hoặc gặp sự cố và không gửi lệnh commit trước khi `ttl_ms` kết thúc, tiến trình quét dọn chủ động sẽ tự động hủy reservation và hoàn trả toàn bộ số lượng quota đã giữ lại về kho.

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

# Chạy kiểm thử tự động toàn diện (20/20 test target)
ctest --test-dir build --output-on-failure
```

#### Khởi Chạy Máy Chủ

```bash
# Lắng nghe cổng nhị phân 63779 và cổng HTTP 8080 với chu kỳ quét dọn dẹp 50ms
./build/gatekeeper --port 63779 --http-port 8080 --timer 50 --log terminal
```

Các tham số dòng lệnh:
- `-p, --port <port>`: Cổng lắng nghe giao thức nhị phân GKWP (mặc định: `63779`).
- `--http-port <port>`: Cổng lắng nghe HTTP REST API (mặc định: `0`, tức tắt).
- `-t, --timer <ms>`: Chu kỳ mili-giây quét dọn dẹp key hết hạn (`-1` để tắt).
- `-l, --log <mode>`: Chế độ ghi log (`none`, `terminal`, `file`).
- `-d, --log-dir <dir>`: Thư mục chứa file log khi dùng chế độ file.

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

### 5. Hướng Dẫn Xây Dựng SDK và Middleware

Lập trình viên có thể tích hợp GateKeeper vào bất kỳ ngôn ngữ nào thông qua giao thức nhị phân GKWP hoặc giao diện HTTP REST.

#### Phương Án 1: Xây Dựng Middleware Qua HTTP REST (Khuyến Nghị Cho Web Framework)

Để viết middleware rate-limit cho Go (Gin/Fiber), Node.js (Express/Fastify/NestJS), Python (FastAPI/Django), hoặc Java (Spring Boot), luồng xử lý chuẩn gồm 4 bước:

```
[ HTTP Request Từ Client ]
            |
            v
[ Bước 1: Trích xuất Subject Key (Client IP, API Key, User ID) ]
            |
            v
[ Bước 2: Gửi POST tới GateKeeper /v1/rate-limit/check ]
            |
      +-----+------------------------+
      |                              |
[ Mã 200 OK: Hợp lệ ]       [ Mã 429: Vượt Ngưỡng ]
      |                              |
      v                              v
Gắn các header X-RateLimit    Gắn các header X-RateLimit và Retry-After
Cho phép request đi tiếp      Ngắt request: Trả về JSON lỗi 429
```

Các nguyên tắc kỹ thuật quan trọng khi triển khai middleware:
- **Tái sử dụng kết nối (Connection Pooling)**: Luôn bật `keep-alive` để tái sử dụng kết nối HTTP nhằm triệt tiêu chi phí bắt tay TCP trong mỗi request.
- **Chính sách Fail-Open và Fail-Closed**:
  - Với các API công cộng phục vụ người dùng thông thường: Cấu hình Fail-Open (cho phép request đi tiếp nếu không thể kết nối tới GateKeeper) để tránh làm gián đoạn toàn bộ hệ thống.
  - Với các API nhạy cảm (đăng nhập, chống tấn công brute-force, thanh toán): Cấu hình Fail-Closed (chặn request và trả về lỗi 503) để đảm bảo an toàn tối đa.
- **Truyền tiếp Header**: Luôn chuyển tiếp `X-RateLimit-Limit`, `X-RateLimit-Remaining`, và `X-RateLimit-Reset` về phía client frontend để ứng dụng tự điều tiết tần suất gửi request.

#### Phương Án 2: Kết Nối Bằng Giao Thức Nhị Phân GKWP (Độ Trễ Tối Thiểu)

Dành cho các dịch vụ nội bộ (microservices) cần thông lượng cao:
1. Thiết lập kết nối TCP socket duy trì lâu dài tới cổng `63779`.
2. Tạo chuỗi JSON theo cấu trúc: `{"id": id, "op": op, "body": args}`.
3. Chèn 4 byte tiền tố chứa độ dài chuỗi JSON (dưới dạng Big-Endian 32-bit).
4. Gửi toàn bộ buffer qua socket.
5. Đọc 4 byte đầu tiên từ phản hồi để xác định độ dài gói tin, sau đó đọc đủ số byte payload tương ứng.

Mã nguồn triển khai hoàn chỉnh được cung cấp sẵn tại thư mục `sdk/go/` và `sdk/nodejs/`.

---
