#!/usr/bin/env python3
"""
GateKeeper Comprehensive Benchmark Suite
Compares GateKeeper (GKWP/2, HTTP) with Redis 7, Dragonfly, and NGINX.
"""

import asyncio
import struct
import json
import time
import socket
import argparse
import statistics
import subprocess
import os

GK_PORT = 63779
GK_HTTP_PORT = 8080
REDIS_PORT = 6379
DRAGONFLY_PORT = 6380
NGINX_HTTP_PORT = 8081

# ----------------- GKWP/2 Client -----------------
class Gkwp2Client:
    def __init__(self, host="127.0.0.1", port=GK_PORT):
        self.host = host
        self.port = port
        self.reader = None
        self.writer = None
        self.next_req_id = 1

    async def connect(self):
        self.reader, self.writer = await asyncio.open_connection(self.host, self.port)

    async def send_cmd(self, op: str, body: dict, stream_id: int = 1):
        req_num = self.next_req_id
        self.next_req_id += 1
        req_id_str = f"req-{req_num}"
        payload = json.dumps({"id": req_id_str, "op": op, "body": body}).encode("utf-8")

        # 24-byte GKWP/2 Header:
        # magic (2B): 0x474B
        # version (1B): 0x02
        # flags (1B): 0x08 (kEndStream)
        # msg_type (1B): 0x01 (Request)
        # reserved (3B): b'\x00\x00\x00'
        # request_id (8B): req_num
        # stream_id (4B): stream_id
        # payload_len (4B): len(payload)
        header = struct.pack(">HBBB3sQII", 0x474B, 0x02, 0x08, 0x01, b"\x00\x00\x00", req_num, stream_id, len(payload))
        self.writer.write(header + payload)
        await self.writer.drain()

        # Read 24-byte response header
        resp_hdr = await self.reader.readexactly(24)
        magic, ver, flags, msg_type, _, resp_req_id, resp_stream_id, payload_len = struct.unpack(">HBBB3sQII", resp_hdr)
        resp_bytes = await self.reader.readexactly(payload_len)
        try:
            return json.loads(resp_bytes.decode("utf-8"))
        except Exception:
            return {"raw": resp_bytes.decode("utf-8", errors="ignore")}


    async def send_binary_set(self, key: str, value: str, ttl_ms: int = 0, condition: int = 0, stream_id: int = 1):
        req_num = self.next_req_id
        self.next_req_id += 1
        key_bytes = key.encode("utf-8")
        val_bytes = value.encode("utf-8")
        payload = struct.pack(">HH", 0x0010, len(key_bytes)) + key_bytes + struct.pack(">I", len(val_bytes)) + val_bytes + struct.pack(">QB", ttl_ms, condition)
        header = struct.pack(">HBBB3sQII", 0x474B, 0x02, 0x28, 0x01, b"\x00\x00\x00", req_num, stream_id, len(payload))
        self.writer.write(header + payload)
        await self.writer.drain()

        resp_hdr = await self.reader.readexactly(24)
        magic, ver, flags, msg_type, _, resp_req_id, resp_stream_id, payload_len = struct.unpack(">HBBB3sQII", resp_hdr)
        resp_bytes = await self.reader.readexactly(payload_len)
        return resp_bytes[0] == 0

    async def send_binary_get(self, key: str, stream_id: int = 1):
        req_num = self.next_req_id
        self.next_req_id += 1
        key_bytes = key.encode("utf-8")
        payload = struct.pack(">HH", 0x0011, len(key_bytes)) + key_bytes
        header = struct.pack(">HBBB3sQII", 0x474B, 0x02, 0x28, 0x01, b"\x00\x00\x00", req_num, stream_id, len(payload))
        self.writer.write(header + payload)
        await self.writer.drain()

        resp_hdr = await self.reader.readexactly(24)
        magic, ver, flags, msg_type, _, resp_req_id, resp_stream_id, payload_len = struct.unpack(">HBBB3sQII", resp_hdr)
        resp_bytes = await self.reader.readexactly(payload_len)
        if resp_bytes[0] == 0 and len(resp_bytes) >= 5:
            val_len = struct.unpack(">I", resp_bytes[1:5])[0]
            return resp_bytes[5:5 + val_len].decode("utf-8", errors="ignore")
        return None

    async def send_binary_rate_limit(self, key: str, limit: int, window_ms: int, cost: int = 1, stream_id: int = 1):
        req_num = self.next_req_id
        self.next_req_id += 1
        key_bytes = key.encode("utf-8")
        payload = struct.pack(">HH", 0x0100, len(key_bytes)) + key_bytes + struct.pack(">QQI", limit, window_ms, cost)
        header = struct.pack(">HBBB3sQII", 0x474B, 0x02, 0x28, 0x01, b"\x00\x00\x00", req_num, stream_id, len(payload))
        self.writer.write(header + payload)
        await self.writer.drain()

        resp_hdr = await self.reader.readexactly(24)
        magic, ver, flags, msg_type, _, resp_req_id, resp_stream_id, payload_len = struct.unpack(">HBBB3sQII", resp_hdr)
        resp_bytes = await self.reader.readexactly(payload_len)
        if resp_bytes[0] == 0 and len(resp_bytes) >= 18:
            allowed = resp_bytes[1] == 1
            remaining = struct.unpack(">Q", resp_bytes[2:10])[0]
            retry_after = struct.unpack(">Q", resp_bytes[10:18])[0]
            return {"allowed": allowed, "remaining": remaining, "retry_after_ms": retry_after}
        return {"allowed": False, "remaining": 0, "retry_after_ms": 0}

    async def close(self):
        if self.writer:
            self.writer.close()
            try:
                await self.writer.wait_closed()
            except Exception:
                pass


# ----------------- Redis RESP Client -----------------
class RedisRespClient:
    def __init__(self, host="127.0.0.1", port=REDIS_PORT):
        self.host = host
        self.port = port
        self.reader = None
        self.writer = None

    async def connect(self):
        self.reader, self.writer = await asyncio.open_connection(self.host, self.port)

    async def send_cmd(self, *args):
        # Format as RESP Array of Bulk Strings
        cmd = f"*{len(args)}\r\n"
        for arg in args:
            s_arg = str(arg)
            b_arg = s_arg.encode("utf-8")
            cmd += f"${len(b_arg)}\r\n{s_arg}\r\n"
        self.writer.write(cmd.encode("utf-8"))
        await self.writer.drain()

        # Read response line
        line = await self.reader.readline()
        if not line:
            return None
        prefix = chr(line[0])
        if prefix in ("+", "-", ":"):
            return line[1:-2].decode("utf-8")
        elif prefix == "$":
            length = int(line[1:-2])
            if length == -1:
                return None
            data = await self.reader.readexactly(length + 2)
            return data[:-2].decode("utf-8")
        elif prefix == "*":
            count = int(line[1:-2])
            res = []
            for _ in range(count):
                sub_line = await self.reader.readline()
                sub_prefix = chr(sub_line[0])
                if sub_prefix == ":":
                    res.append(int(sub_line[1:-2]))
                elif sub_prefix == "$":
                    l = int(sub_line[1:-2])
                    if l == -1:
                        res.append(None)
                    else:
                        d = await self.reader.readexactly(l + 2)
                        res.append(d[:-2].decode("utf-8"))
            return res
        return line.decode("utf-8")


    async def send_binary_set(self, key: str, value: str, ttl_ms: int = 0, condition: int = 0, stream_id: int = 1):
        req_num = self.next_req_id
        self.next_req_id += 1
        key_bytes = key.encode("utf-8")
        val_bytes = value.encode("utf-8")
        payload = struct.pack(">HH", 0x0010, len(key_bytes)) + key_bytes + struct.pack(">I", len(val_bytes)) + val_bytes + struct.pack(">QB", ttl_ms, condition)
        header = struct.pack(">HBBB3sQII", 0x474B, 0x02, 0x28, 0x01, b"\x00\x00\x00", req_num, stream_id, len(payload))
        self.writer.write(header + payload)
        await self.writer.drain()

        resp_hdr = await self.reader.readexactly(24)
        magic, ver, flags, msg_type, _, resp_req_id, resp_stream_id, payload_len = struct.unpack(">HBBB3sQII", resp_hdr)
        resp_bytes = await self.reader.readexactly(payload_len)
        return resp_bytes[0] == 0

    async def send_binary_get(self, key: str, stream_id: int = 1):
        req_num = self.next_req_id
        self.next_req_id += 1
        key_bytes = key.encode("utf-8")
        payload = struct.pack(">HH", 0x0011, len(key_bytes)) + key_bytes
        header = struct.pack(">HBBB3sQII", 0x474B, 0x02, 0x28, 0x01, b"\x00\x00\x00", req_num, stream_id, len(payload))
        self.writer.write(header + payload)
        await self.writer.drain()

        resp_hdr = await self.reader.readexactly(24)
        magic, ver, flags, msg_type, _, resp_req_id, resp_stream_id, payload_len = struct.unpack(">HBBB3sQII", resp_hdr)
        resp_bytes = await self.reader.readexactly(payload_len)
        if resp_bytes[0] == 0 and len(resp_bytes) >= 5:
            val_len = struct.unpack(">I", resp_bytes[1:5])[0]
            return resp_bytes[5:5 + val_len].decode("utf-8", errors="ignore")
        return None

    async def send_binary_rate_limit(self, key: str, limit: int, window_ms: int, cost: int = 1, stream_id: int = 1):
        req_num = self.next_req_id
        self.next_req_id += 1
        key_bytes = key.encode("utf-8")
        payload = struct.pack(">HH", 0x0100, len(key_bytes)) + key_bytes + struct.pack(">QQI", limit, window_ms, cost)
        header = struct.pack(">HBBB3sQII", 0x474B, 0x02, 0x28, 0x01, b"\x00\x00\x00", req_num, stream_id, len(payload))
        self.writer.write(header + payload)
        await self.writer.drain()

        resp_hdr = await self.reader.readexactly(24)
        magic, ver, flags, msg_type, _, resp_req_id, resp_stream_id, payload_len = struct.unpack(">HBBB3sQII", resp_hdr)
        resp_bytes = await self.reader.readexactly(payload_len)
        if resp_bytes[0] == 0 and len(resp_bytes) >= 18:
            allowed = resp_bytes[1] == 1
            remaining = struct.unpack(">Q", resp_bytes[2:10])[0]
            retry_after = struct.unpack(">Q", resp_bytes[10:18])[0]
            return {"allowed": allowed, "remaining": remaining, "retry_after_ms": retry_after}
        return {"allowed": False, "remaining": 0, "retry_after_ms": 0}

    async def close(self):
        if self.writer:
            self.writer.close()
            try:
                await self.writer.wait_closed()
            except Exception:
                pass


# ----------------- Benchmark Helper -----------------
def calculate_stats(latencies_ms):
    if not latencies_ms:
        return {}
    latencies_ms.sort()
    return {
        "count": len(latencies_ms),
        "p50": statistics.median(latencies_ms),
        "p90": latencies_ms[int(len(latencies_ms) * 0.90)],
        "p95": latencies_ms[int(len(latencies_ms) * 0.95)],
        "p99": latencies_ms[int(len(latencies_ms) * 0.99)],
        "p99.9": latencies_ms[min(len(latencies_ms) - 1, int(len(latencies_ms) * 0.999))],
        "avg": statistics.mean(latencies_ms),
        "min": min(latencies_ms),
        "max": max(latencies_ms),
    }


# ----------------- Scenario 1: Raw KV -----------------
async def bench_raw_kv(target="gk", concurrency=50, total_reqs=10000):
    reqs_per_worker = total_reqs // concurrency
    all_latencies = []

    async def worker(worker_id):
        client = Gkwp2Client(port=GK_PORT) if target == "gk" else RedisRespClient(port=REDIS_PORT if target == "redis" else DRAGONFLY_PORT)
        try:
            await client.connect()
        except Exception as e:
            return

        worker_latencies = []
        for i in range(reqs_per_worker):
            key = f"kv:worker_{worker_id}:key_{i % 100}"
            val = f"val_{i}"
            t0 = time.perf_counter()
            if target == "gk":
                await client.send_binary_set(key, val)
                await client.send_binary_get(key)
            else:
                await client.send_cmd("SET", key, val)
                await client.send_cmd("GET", key)
            t1 = time.perf_counter()
            worker_latencies.append((t1 - t0) * 1000.0)

        await client.close()
        all_latencies.extend(worker_latencies)

    start_time = time.perf_counter()
    tasks = [worker(w) for w in range(concurrency)]
    await asyncio.gather(*tasks)
    total_time = time.perf_counter() - start_time

    stats = calculate_stats(all_latencies)
    qps = (len(all_latencies) * 2) / total_time
    stats["qps"] = qps
    stats["total_time"] = total_time
    return stats


# ----------------- Scenario 2: Rate Limit -----------------
async def bench_rate_limit(target="gk", concurrency=50, total_reqs=10000):
    reqs_per_worker = total_reqs // concurrency
    all_latencies = []

    lua_code = ""
    if target in ("redis", "dragonfly"):
        with open("/home/caftun/Code/C++/GateKeeper/tools/bench/services/configs/lua/sliding_window.lua", "r") as f:
            lua_code = f.read()

    async def worker(worker_id):
        client = Gkwp2Client(port=GK_PORT) if target == "gk" else RedisRespClient(port=REDIS_PORT if target == "redis" else DRAGONFLY_PORT)
        try:
            await client.connect()
        except Exception as e:
            return

        worker_latencies = []
        now_ms = int(time.time() * 1000)
        for i in range(reqs_per_worker):
            key = f"rate:user_{worker_id % 20}"
            t0 = time.perf_counter()
            if target == "gk":
                await client.send_binary_rate_limit(key, 500000, 60000, 1)
            else:
                await client.send_cmd("EVAL", lua_code, 1, key, 500000, 60000, now_ms + i, 1)
            t1 = time.perf_counter()
            worker_latencies.append((t1 - t0) * 1000.0)

        await client.close()
        all_latencies.extend(worker_latencies)

    start_time = time.perf_counter()
    tasks = [worker(w) for w in range(concurrency)]
    await asyncio.gather(*tasks)
    total_time = time.perf_counter() - start_time

    stats = calculate_stats(all_latencies)
    qps = len(all_latencies) / total_time
    stats["qps"] = qps
    stats["total_time"] = total_time
    return stats


# ----------------- Scenario 3: Two-Phase Quota -----------------
async def bench_quota_reservation(concurrency=50, total_reqs=5000):
    reqs_per_worker = total_reqs // concurrency
    all_latencies = []

    init_client = Gkwp2Client(port=GK_PORT)
    await init_client.connect()
    await init_client.send_cmd("GK.QUOTA_INIT", {"key": "quota:pool_1", "quota": 10000000})
    await init_client.close()

    async def worker(worker_id):
        client = Gkwp2Client(port=GK_PORT)
        try:
            await client.connect()
        except Exception:
            return

        worker_latencies = []
        for i in range(reqs_per_worker):
            t0 = time.perf_counter()
            res = await client.send_cmd("GK.RESERVE", {"key": "quota:pool_1", "amount": 10, "ttl_ms": 5000})
            resid = res.get("result", {}).get("reservation_id")
            if resid:
                await client.send_cmd("GK.COMMIT", {"key": "quota:pool_1", "reservation_id": resid, "actual_amount": 8})
            t1 = time.perf_counter()
            worker_latencies.append((t1 - t0) * 1000.0)

        await client.close()
        all_latencies.extend(worker_latencies)

    start_time = time.perf_counter()
    tasks = [worker(w) for w in range(concurrency)]
    await asyncio.gather(*tasks)
    total_time = time.perf_counter() - start_time

    stats = calculate_stats(all_latencies)
    stats["qps"] = (len(all_latencies) * 2) / total_time
    stats["total_time"] = total_time
    return stats


# ----------------- Scenario 4: Single-Flight vs Spin-Polling -----------------
async def bench_single_flight(duplicate_clients=100):
    key = f"idem:order_{int(time.time()*1000)}"

    # GateKeeper Test
    gk_latencies = []
    async def gk_client_task(cid):
        client = Gkwp2Client(port=GK_PORT)
        await client.connect()
        t0 = time.perf_counter()
        resp = await client.send_cmd("GK.IDEM_BEGIN", {"key": key, "request_hash": "hash_123", "ttl_ms": 5000})
        status = resp.get("result", {}).get("status")
        if status == "NEW":
            await asyncio.sleep(0.05)
            await client.send_cmd("GK.IDEM_COMPLETE", {"key": key, "response": '{"order_id":999,"status":"ok"}'})
        t1 = time.perf_counter()
        gk_latencies.append((t1 - t0) * 1000.0)
        await client.close()

    t_start = time.perf_counter()
    await asyncio.gather(*[gk_client_task(i) for i in range(duplicate_clients)])
    gk_total_time = (time.perf_counter() - t_start) * 1000.0

    # Redis Test (Spin-Poll Simulation)
    redis_latencies = []
    redis_polls_count = [0]
    async def redis_client_task(cid):
        client = RedisRespClient(port=REDIS_PORT)
        await client.connect()
        t0 = time.perf_counter()
        acquired = await client.send_cmd("SET", f"lock:{key}", f"owner_{cid}", "NX", "PX", 5000)
        if acquired == "OK":
            await asyncio.sleep(0.05)
            await client.send_cmd("SET", key, '{"order_id":999,"status":"ok"}', "PX", 5000)
            await client.send_cmd("DEL", f"lock:{key}")
        else:
            while True:
                redis_polls_count[0] += 1
                val = await client.send_cmd("GET", key)
                if val is not None:
                    break
                await asyncio.sleep(0.01)
        t1 = time.perf_counter()
        redis_latencies.append((t1 - t0) * 1000.0)
        await client.close()

    t_start = time.perf_counter()
    await asyncio.gather(*[redis_client_task(i) for i in range(duplicate_clients)])
    redis_total_time = (time.perf_counter() - t_start) * 1000.0

    return {
        "clients": duplicate_clients,
        "gk_total_ms": gk_total_time,
        "gk_p99_ms": calculate_stats(gk_latencies).get("p99", 0),
        "redis_total_ms": redis_total_time,
        "redis_p99_ms": calculate_stats(redis_latencies).get("p99", 0),
        "redis_total_poll_packets": redis_polls_count[0],
    }


# ----------------- Scenario 5: HTTP Rate Limit (wrk) -----------------
def bench_http_wrk(target="gk", threads=2, connections=50, duration_sec=5):
    url = f"http://127.0.0.1:{GK_HTTP_PORT}/v1/rate-limit/check" if target == "gk" else f"http://127.0.0.1:{NGINX_HTTP_PORT}/rate-limit"
    if target == "gk":
        lua_post = """
wrk.method = "POST"
wrk.body   = '{"tenant":"acme","subject":"user_1","resource":"api:chat","limit":500000,"window_ms":60000}'
wrk.headers["Content-Type"] = "application/json"
"""
        with open("/tmp/wrk_post.lua", "w") as f:
            f.write(lua_post)
        cmd = ["wrk", f"-t{threads}", f"-c{connections}", f"-d{duration_sec}s", "-s", "/tmp/wrk_post.lua", "--latency", url]
    else:
        cmd = ["wrk", f"-t{threads}", f"-c{connections}", f"-d{duration_sec}s", "--latency", url]

    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=duration_sec + 10)
        return proc.stdout
    except Exception as e:
        return str(e)


# ----------------- Main Entrypoint -----------------
async def main():
    parser = argparse.ArgumentParser(description="GateKeeper Benchmark Suite")
    parser.add_argument("--scenario", choices=["all", "kv", "rate_limit", "quota", "single_flight", "http"], default="all")
    parser.add_argument("--concurrency", type=int, default=20)
    parser.add_argument("--requests", type=int, default=2000)
    args = parser.parse_args()

    print("======================================================================")
    print("           GATEKEEPER BENCHMARK EXECUTION SUITE                       ")
    print("======================================================================")

    if args.scenario in ("all", "kv"):
        print("\n[Scenario 1] Raw Key-Value Operations (SET + GET):")
        for target in ["gk", "redis", "dragonfly"]:
            stats = await bench_raw_kv(target=target, concurrency=args.concurrency, total_reqs=args.requests)
            print(f"  Target: {target.upper():<10} | QPS: {stats.get('qps',0):>10.2f} ops/s | p50: {stats.get('p50',0):>6.2f}ms | p95: {stats.get('p95',0):>6.2f}ms | p99: {stats.get('p99',0):>6.2f}ms")

    if args.scenario in ("all", "rate_limit"):
        print("\n[Scenario 2] Distributed Sliding-Window Rate Limiting (Binary/RPC):")
        for target in ["gk", "redis", "dragonfly"]:
            stats = await bench_rate_limit(target=target, concurrency=args.concurrency, total_reqs=args.requests)
            print(f"  Target: {target.upper():<10} | QPS: {stats.get('qps',0):>10.2f} ops/s | p50: {stats.get('p50',0):>6.2f}ms | p95: {stats.get('p95',0):>6.2f}ms | p99: {stats.get('p99',0):>6.2f}ms")

    if args.scenario in ("all", "quota"):
        print("\n[Scenario 3] Two-Phase Quota Reservation (RESERVE + COMMIT):")
        stats = await bench_quota_reservation(concurrency=args.concurrency, total_reqs=args.requests // 2)
        print(f"  Target: GATEKEEPER | QPS: {stats.get('qps',0):>10.2f} ops/s | p50: {stats.get('p50',0):>6.2f}ms | p95: {stats.get('p95',0):>6.2f}ms | p99: {stats.get('p99',0):>6.2f}ms")

    if args.scenario in ("all", "single_flight"):
        print("\n[Scenario 4] Single-Flight Coalescing vs Redis Spin-Polling (Thundering Herd):")
        res = await bench_single_flight(duplicate_clients=50)
        print(f"  GateKeeper (Parking): Total: {res['gk_total_ms']:.2f}ms | p99: {res['gk_p99_ms']:.2f}ms | Poll Packets: 0 (Broadcast)")
        print(f"  Redis (Spin-Poll)   : Total: {res['redis_total_ms']:.2f}ms | p99: {res['redis_p99_ms']:.2f}ms | Poll Packets: {res['redis_total_poll_packets']} network polls")

    if args.scenario in ("all", "http"):
        print("\n[Scenario 5] HTTP Rate Limit Surface (wrk 50 connections, 5s):")
        print("--- NGINX (C-based edge reverse proxy limit_req) ---")
        print(bench_http_wrk(target="nginx", connections=50, duration_sec=5))
        print("--- GateKeeper (C++20 In-Memory Engine REST API) ---")
        print(bench_http_wrk(target="gk", connections=50, duration_sec=5))

if __name__ == "__main__":
    asyncio.run(main())
