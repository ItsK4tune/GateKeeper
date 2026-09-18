import concurrent.futures
import json
import socket
import subprocess
import sys
import time
import urllib.request
import urllib.error


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def get_free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


server_bin = sys.argv[1]
port = get_free_port()
http_port = get_free_port()
while http_port == port:
    http_port = get_free_port()

process = subprocess.Popen(
    [server_bin, "--port", str(port), "--http-port", str(http_port)],
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True
)

try:
    deadline = time.monotonic() + 5
    while True:
        require(process.poll() is None, "server exited before listening")
        try:
            with socket.create_connection(("127.0.0.1", http_port), timeout=0.2):
                break
        except OSError:
            require(time.monotonic() < deadline, "server did not start listening")
            time.sleep(0.02)

    url = f"http://127.0.0.1:{http_port}/v1/rate-limit/check"
    payload = json.dumps({
        "key": f"concurrency_test_key_{int(time.time()*1000)}",
        "limit": 20,
        "window_ms": 60000,
        "cost": 1
    }).encode("utf-8")

    total_requests = 500
    concurrency = 50

    def send_request(_):
        t0 = time.perf_counter()
        req = urllib.request.Request(
            url,
            data=payload,
            headers={"Content-Type": "application/json"},
            method="POST"
        )
        try:
            with urllib.request.urlopen(req, timeout=5) as resp:
                status = resp.status
                body = resp.read()
        except urllib.error.HTTPError as e:
            status = e.code
            body = e.read()
        duration_ms = (time.perf_counter() - t0) * 1000
        return status, duration_ms, body

    start_time = time.perf_counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as pool:
        results = list(pool.map(send_request, range(total_requests)))
    total_duration = time.perf_counter() - start_time

    allowed_count = sum(1 for status, _, _ in results if status == 200)
    rejected_count = sum(1 for status, _, _ in results if status == 429)
    other_count = sum(1 for status, _, _ in results if status not in (200, 429))

    durations = sorted(d for _, d, _ in results)
    p50 = durations[int(len(durations) * 0.50)]
    p95 = durations[int(len(durations) * 0.95)]
    p99 = durations[int(len(durations) * 0.99)]
    rps = total_requests / total_duration

    print(f"--- Concurrency Test Results ---")
    print(f"Total Requests: {total_requests} (concurrency={concurrency})")
    print(f"Allowed (200 OK): {allowed_count} (Expected: 20)")
    print(f"Rejected (429):   {rejected_count} (Expected: {total_requests - 20})")
    print(f"Other Statuses:   {other_count} (Expected: 0)")
    print(f"Throughput:       {rps:.1f} req/sec")
    print(f"Latency p50:      {p50:.2f} ms")
    print(f"Latency p95:      {p95:.2f} ms")
    print(f"Latency p99:      {p99:.2f} ms")

    require(allowed_count == 20, f"Race condition detected! Allowed {allowed_count} instead of exactly 20")
    require(rejected_count == total_requests - 20, f"Expected {total_requests - 20} rejected, got {rejected_count}")
    require(other_count == 0, f"Unexpected responses with status other than 200/429: {other_count}")
    print("Zero race condition verified successfully!")

finally:
    process.terminate()
    process.communicate(timeout=3)
