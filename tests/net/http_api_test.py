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
        require(process.poll() is None, "server exited prematurely")
        try:
            with socket.create_connection(("127.0.0.1", http_port), timeout=0.2):
                break
        except OSError:
            require(time.monotonic() < deadline, "server did not start listening on http port")
            time.sleep(0.02)

    base_url = f"http://127.0.0.1:{http_port}"

    # 1. Test GET /healthz
    req = urllib.request.Request(f"{base_url}/healthz", method="GET")
    with urllib.request.urlopen(req, timeout=3) as resp:
        require(resp.status == 200, f"healthz returned status {resp.status}")
        body = json.loads(resp.read().decode())
        require(body.get("status") == "ok", f"healthz returned bad body {body}")

    # 2. Test POST /v1/rate-limit/check (Allowed -> Rejected)
    for i in range(5):
        payload = json.dumps({
            "tenant": "tenant_test",
            "subject": "sub_test",
            "resource": "res_test",
            "limit": 5,
            "window_ms": 60000
        }).encode()
        req = urllib.request.Request(
            f"{base_url}/v1/rate-limit/check",
            data=payload,
            headers={"Content-Type": "application/json"},
            method="POST"
        )
        with urllib.request.urlopen(req, timeout=3) as resp:
            require(resp.status == 200, f"rate-limit check #{i+1} status {resp.status}")
            data = json.loads(resp.read().decode())
            expected_remaining = 5 - (i + 1)
            require(data.get("allowed") is True, f"expected allowed=True at {i+1}")
            require(data.get("remaining") == expected_remaining, f"expected remaining={expected_remaining}, got {data.get('remaining')}")
            require(resp.headers.get("X-RateLimit-Limit") == "5", "X-RateLimit-Limit header missing")
            require(resp.headers.get("X-RateLimit-Remaining") == str(expected_remaining), "X-RateLimit-Remaining header mismatch")

    # 6th request: Rate limited (429)
    req = urllib.request.Request(
        f"{base_url}/v1/rate-limit/check",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST"
    )
    try:
        urllib.request.urlopen(req, timeout=3)
        require(False, "6th request should have returned 429")
    except urllib.error.HTTPError as e:
        require(e.code == 429, f"expected 429, got {e.code}")
        data = json.loads(e.read().decode())
        require(data.get("allowed") is False, "expected allowed=False on 429")
        require(data.get("remaining") == 0, "expected remaining=0 on 429")
        require(data.get("retry_after_ms", 0) > 0, "expected retry_after_ms > 0")
        require(e.headers.get("Retry-After") is not None, "Retry-After header missing")
        require(e.headers.get("X-RateLimit-Limit") == "5", "X-RateLimit-Limit header missing on 429")
        require(e.headers.get("X-RateLimit-Remaining") == "0", "X-RateLimit-Remaining header should be 0")

    # 3. Test Two-Phase Quota Reservation via HTTP
    # 3.1 Init Quota
    init_data = json.dumps({"key": "http_quota", "quota": 100}).encode()
    req = urllib.request.Request(f"{base_url}/v1/quota/init", data=init_data, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=3) as resp:
        require(resp.status == 200, "init quota failed")
        data = json.loads(resp.read().decode())
        require(data.get("ok") is True and data.get("quota") == 100, f"bad init quota response {data}")

    # 3.2 Reserve 30
    res_data = json.dumps({"key": "http_quota", "amount": 30, "ttl_ms": 10000}).encode()
    req = urllib.request.Request(f"{base_url}/v1/quota/reserve", data=res_data, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=3) as resp:
        require(resp.status == 200, "reserve quota failed")
        data = json.loads(resp.read().decode())
        require(data.get("reserved") is True, "reserve flag is false")
        require(data.get("remaining") == 70, f"remaining should be 70, got {data.get('remaining')}")
        res_id = data.get("reservation_id")
        require(bool(res_id), "missing reservation_id")

    # 3.3 Commit 25 (refund 5)
    commit_data = json.dumps({"key": "http_quota", "reservation_id": res_id, "actual_amount": 25}).encode()
    req = urllib.request.Request(f"{base_url}/v1/quota/commit", data=commit_data, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=3) as resp:
        require(resp.status == 200, "commit quota failed")
        data = json.loads(resp.read().decode())
        require(data.get("committed") is True, "committed flag false")
        require(data.get("actual_amount") == 25, "actual_amount mismatch")
        require(data.get("refunded") == 5, "refunded mismatch")
        require(data.get("remaining") == 75, f"remaining should be 75, got {data.get('remaining')}")

    # 3.4 Reserve 50 and Rollback
    res_data2 = json.dumps({"key": "http_quota", "amount": 50, "ttl_ms": 10000}).encode()
    req = urllib.request.Request(f"{base_url}/v1/quota/reserve", data=res_data2, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=3) as resp:
        data = json.loads(resp.read().decode())
        require(data.get("reserved") is True, "reserve 2 failed")
        res_id2 = data.get("reservation_id")
        require(data.get("remaining") == 25, "remaining after reserve 2 should be 25")

    # 3.5 Rollback
    rb_data = json.dumps({"key": "http_quota", "reservation_id": res_id2}).encode()
    req = urllib.request.Request(f"{base_url}/v1/quota/rollback", data=rb_data, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=3) as resp:
        require(resp.status == 200, "rollback quota failed")
        data = json.loads(resp.read().decode())
        require(data.get("rolled_back") is True, "rolled_back flag false")
        require(data.get("refunded") == 50, "refunded amount mismatch")
        require(data.get("remaining") == 75, f"remaining after rollback should be 75, got {data.get('remaining')}")

    # 4. Error Cases
    # 4.1 404 Not Found
    try:
        urllib.request.urlopen(f"{base_url}/unknown/endpoint", timeout=3)
        require(False, "expected 404")
    except urllib.error.HTTPError as e:
        require(e.code == 404, f"expected 404, got {e.code}")

    # 4.2 405 Method Not Allowed
    try:
        req = urllib.request.Request(f"{base_url}/healthz", method="POST")
        urllib.request.urlopen(req, timeout=3)
        require(False, "expected 405")
    except urllib.error.HTTPError as e:
        require(e.code == 405, f"expected 405, got {e.code}")

    # 4.3 400 Bad Request
    try:
        req = urllib.request.Request(f"{base_url}/v1/rate-limit/check", data=b"{invalid json}", headers={"Content-Type": "application/json"}, method="POST")
        urllib.request.urlopen(req, timeout=3)
        require(False, "expected 400")
    except urllib.error.HTTPError as e:
        require(e.code == 400, f"expected 400, got {e.code}")

    print("HTTP API tests passed!")

finally:
    process.terminate()
    process.communicate(timeout=3)
