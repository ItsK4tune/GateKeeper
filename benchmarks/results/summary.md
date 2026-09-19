# GateKeeper Cross-Protocol Benchmark & Performance Evaluation

Bảng tổng hợp kết quả đo đạc hiệu năng thực tế trên cùng phần cứng máy chủ với 50 kết nối đồng thời (`Concurrency: 50`) qua 5 chuẩn giao thức: **GKWP/2**, **GKWP/1**, **Redis RESP3**, **Redis RESP2**, và **HTTP/1.1**.

## 1. Full Cross-Protocol Performance Matrix

| Giao thức | Thao tác | Tổng Request | Thành công | Throughput (QPS) | P50 Latency | P90 Latency | P99 Latency | Max Latency |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **GKWP/2** | `PING` (Baseline I/O) | 50,000 | **100.00%** | **25,124.50 QPS** | 1.83 ms | 2.91 ms | 4.46 ms | 9.51 ms |
| **GKWP/2** | `GK.RATE_LIMIT` (Sliding Window) | 50,000 | **100.00%** | **25,577.46 QPS** | 1.79 ms | 2.81 ms | 4.24 ms | 8.04 ms |
| **GKWP/2** | `SET` (Key-Value Write) | 50,000 | **100.00%** | **21,244.88 QPS** | 2.16 ms | 3.19 ms | 4.91 ms | 9.26 ms |
| **GKWP/2** | `GET` (Key-Value Read) | 50,000 | **100.00%** | **23,196.56 QPS** | 1.99 ms | 3.04 ms | 4.70 ms | 8.09 ms |
| **GKWP/1** | `PING` (Baseline I/O) | 50,000 | **100.00%** | **24,882.00 QPS** | 1.86 ms | 2.88 ms | 4.28 ms | 8.69 ms |
| **GKWP/1** | `GK.RATE_LIMIT` (Sliding Window) | 50,000 | **100.00%** | **20,546.74 QPS** | 2.26 ms | 3.27 ms | 5.02 ms | 7.13 ms |
| **GKWP/1** | `SET` (Key-Value Write) | 50,000 | **100.00%** | **16,695.43 QPS** | 2.82 ms | 3.81 ms | 6.18 ms | 9.57 ms |
| **GKWP/1** | `GET` (Key-Value Read) | 50,000 | **100.00%** | **16,197.22 QPS** | 2.91 ms | 4.13 ms | 6.78 ms | 24.02 ms |
| **Redis RESP3** | `PING` (Baseline I/O) | 50,000 | **100.00%** | **23,486.43 QPS** | 1.95 ms | 3.00 ms | 4.38 ms | 7.67 ms |
| **Redis RESP3** | `GK.RATE_LIMIT` (Sliding Window) | 50,000 | **100.00%** | **14,960.98 QPS** | 3.13 ms | 4.17 ms | 6.91 ms | 12.12 ms |
| **Redis RESP3** | `SET` (Key-Value Write) | 50,000 | **100.00%** | **11,588.36 QPS** | 4.02 ms | 5.62 ms | 9.14 ms | 24.51 ms |
| **Redis RESP3** | `GET` (Key-Value Read) | 50,000 | **100.00%** | **13,866.68 QPS** | 3.36 ms | 4.62 ms | 7.38 ms | 11.55 ms |
| **Redis RESP2** | `PING` (Baseline I/O) | 50,000 | **100.00%** | **22,477.64 QPS** | 2.04 ms | 3.20 ms | 4.84 ms | 17.38 ms |
| **Redis RESP2** | `GK.RATE_LIMIT` (Sliding Window) | 50,000 | **100.00%** | **15,315.00 QPS** | 3.04 ms | 4.21 ms | 6.71 ms | 11.76 ms |
| **Redis RESP2** | `SET` (Key-Value Write) | 50,000 | **100.00%** | **12,351.33 QPS** | 3.85 ms | 4.92 ms | 8.17 ms | 12.26 ms |
| **Redis RESP2** | `GET` (Key-Value Read) | 50,000 | **100.00%** | **14,546.67 QPS** | 3.23 ms | 4.43 ms | 7.12 ms | 12.74 ms |
| **HTTP/1.1** | `GET /healthz` | 20,000 | **100.00%** | **27,324.83 QPS** | 1.53 ms | 2.99 ms | 6.43 ms | 30.87 ms |
| **HTTP/1.1** | `POST /v1/rate-limit/check` | 20,000 | **100.00%** | **17,307.73 QPS** | 2.57 ms | 4.59 ms | 8.13 ms | 34.75 ms |
| **HTTP/1.1** | `POST /v1/kv/set` | 20,000 | **100.00%** | **17,765.26 QPS** | 2.49 ms | 4.42 ms | 8.68 ms | 39.64 ms |
| **HTTP/1.1** | `GET /v1/kv/get` | 20,000 | **100.00%** | **19,591.13 QPS** | 2.30 ms | 4.00 ms | 7.06 ms | 33.88 ms |

---

## 2. Phân Tích Chuyên Sâu Hiệu Năng Các Chuẩn Giao Thức

### A. GKWP/2 so với GKWP/1
- **Thông lượng (Throughput):**
  - Thao tác `GK.RATE_LIMIT`: GKWP/2 đạt **25,577 QPS** so với GKWP/1 là **20,546 QPS** (tăng **+24.5%**).
  - Thao tác `SET`: GKWP/2 đạt **21,245 QPS** so với GKWP/1 là **16,695 QPS** (tăng **+27.3%**).
  - Thao tác `GET`: GKWP/2 đạt **23,197 QPS** so with GKWP/1 là **16,197 QPS** (tăng **+43.2%**).
- **Độ trễ (P50 & Tail Latency):**
  - P50 của `GET` giảm từ **2.91 ms** xuống **1.99 ms** (-31.6%).
  - Max latency của `GET` giảm từ **24.02 ms** xuống **8.09 ms** (giảm gần 3 lần).
- **Nguyên nhân cốt lõi:**
  - Header nhị phân cố định 24-byte căn chỉnh tự nhiên 8-byte và 4-byte (`sizeof(Header) == 24`), CPU không bị unaligned memory access penalties.
  - Zero-copy framing và tích hợp trực tiếp `stream_id` / `request_id` ở tầng nhị phân thay vì parse chuỗi JSON metadata.

### B. So sánh GKWP/2 với Redis RESP2 và RESP3
- **Thông lượng:**
  - GKWP/2 vượt trội hơn RESP2/RESP3 ở mọi tác vụ nghiệp vụ:
    - `GK.RATE_LIMIT`: GKWP/2 đạt **25,577 QPS** vs RESP2 **15,315 QPS** (+67.0%) vs RESP3 **14,961 QPS** (+71.0%).
    - `SET`: GKWP/2 đạt **21,245 QPS** vs RESP2 **12,351 QPS** (+72.0%) vs RESP3 **11,588 QPS** (+83.3%).
    - `GET`: GKWP/2 đạt **23,197 QPS** vs RESP2 **14,547 QPS** (+59.5%) vs RESP3 **13,867 QPS** (+67.3%).
- **Độ trễ:**
  - Độ trễ P50 của GKWP/2 trên thao tác `SET` chỉ **2.16 ms** so với **3.85 ms** (RESP2) và **4.02 ms** (RESP3).
- **Nhận xét kiến trúc:**
  - Giao thức text/bulk-string như RESP đòi hỏi nhiều lần quét chuỗi CRLF `\r\n` và parsing số ký tự độ dài chuỗi (`std::stol`).
  - GKWP/2 truyền độ dài dạng nhị phân 32-bit big-endian cố định trong header, cho phép đọc payload trực tiếp chỉ với 1 bước tính toán con trỏ bộ nhớ.
  - Khả năng tương thích: GateKeeper hiện có thể phục vụ cả client Redis bản địa (`redis-cli`, `redis-benchmark`, Jedis, go-redis, ...) và client GKWP/2 tối ưu cao trên cùng một cổng TCP thông qua cơ chế tự nhận diện giao thức (Protocol Autodetection).
