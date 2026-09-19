# GateKeeper Benchmark Results & Cross-Protocol Comparison

Bảng tổng hợp kết quả đo đạc hiệu năng thực tế trên cùng phần cứng máy chủ với 50 kết nối đồng thời (`Concurrency: 50`):

## 1. Baseline Performance Summary

| Giao thức | Thao tác | Tổng Request | Thành công | Throughput (QPS) | P50 Latency | P90 Latency | P99 Latency | Max Latency |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **GKWP/1** | `PING` (Baseline I/O) | 50,000 | **100.00%** | **24,882.00 QPS** | 1.86 ms | 2.88 ms | 4.28 ms | 8.69 ms |
| **GKWP/1** | `GK.RATE_LIMIT` (Sliding Window) | 50,000 | **100.00%** | **20,546.74 QPS** | 2.26 ms | 3.27 ms | 5.02 ms | 7.13 ms |
| **GKWP/1** | `SET` (Key-Value Write) | 50,000 | **100.00%** | **16,695.43 QPS** | 2.82 ms | 3.81 ms | 6.18 ms | 9.57 ms |
| **GKWP/1** | `GET` (Key-Value Read) | 50,000 | **100.00%** | **16,197.22 QPS** | 2.91 ms | 4.13 ms | 6.78 ms | 24.02 ms |
| **HTTP/1.1** | `GET /healthz` | 20,000 | **100.00%** | **27,324.83 QPS** | 1.53 ms | 2.99 ms | 6.43 ms | 30.87 ms |
| **HTTP/1.1** | `POST /v1/rate-limit/check` | 20,000 | **100.00%** | **17,307.73 QPS** | 2.57 ms | 4.59 ms | 8.13 ms | 34.75 ms |
| **HTTP/1.1** | `POST /v1/kv/set` | 20,000 | **100.00%** | **17,765.26 QPS** | 2.49 ms | 4.42 ms | 8.68 ms | 39.64 ms |
| **HTTP/1.1** | `GET /v1/kv/get` | 20,000 | **100.00%** | **19,591.13 QPS** | 2.30 ms | 4.00 ms | 7.06 ms | 33.88 ms |

*(Các giao thức tiếp theo: GKWP/2, Redis RESP2, Redis RESP3 sẽ được bổ sung vào bảng này sau khi triển khai trên các nhánh tương ứng).*
