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

# Run with --timer 50 for rapid periodic expiration checks
process = subprocess.Popen(
    [server_bin, "--port", str(port), "--http-port", str(http_port), "--timer", "50"],
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

    base_url = f"http://127.0.0.1:{http_port}"
    key = f"quota_stress_rollback_{int(time.time()*1000)}"

    # 1. Initialize Quota = 100
    init_data = json.dumps({"key": key, "quota": 100}).encode()
    req = urllib.request.Request(f"{base_url}/v1/quota/init", data=init_data, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=3) as resp:
        require(resp.status == 200, "init quota failed")
        data = json.loads(resp.read().decode())
        require(data.get("ok") is True and data.get("quota") == 100, f"quota mismatch: {data}")

    # 2. Worker A reserves 40 quota with short TTL (200ms)
    res_data = json.dumps({"key": key, "amount": 40, "ttl_ms": 200}).encode()
    req = urllib.request.Request(f"{base_url}/v1/quota/reserve", data=res_data, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=3) as resp:
        require(resp.status == 200, "reserve failed")
        data = json.loads(resp.read().decode())
        require(data.get("reserved") is True, "expected reserved=True")
        require(data.get("remaining") == 60, f"expected remaining 60, got {data.get('remaining')}")
        res_id_a = data.get("reservation_id")

    # 3. Verify that trying to reserve 70 right now FAILS (only 60 available)
    res_data_fail = json.dumps({"key": key, "amount": 70, "ttl_ms": 1000}).encode()
    req = urllib.request.Request(f"{base_url}/v1/quota/reserve", data=res_data_fail, headers={"Content-Type": "application/json"}, method="POST")
    try:
        urllib.request.urlopen(req, timeout=3)
        require(False, "reserving 70 when only 60 available should have failed with 429")
    except urllib.error.HTTPError as e:
        require(e.code == 429, f"expected 429, got {e.code}")

    # 4. Simulate Worker A Crash: do NOT commit, do NOT rollback.
    # Sleep 400ms to allow TTL (200ms) to expire and active expiration timer (50ms) to trigger PurgeExpired.
    print("Simulating Worker A crash; waiting for 200ms TTL expiration and active purge...")
    time.sleep(0.4)

    # 5. Worker B attempts to reserve all 100 quota.
    # If auto-rollback worked, the 40 quota was returned to the pool, so all 100 quota is available!
    res_data_b = json.dumps({"key": key, "amount": 100, "ttl_ms": 5000}).encode()
    req = urllib.request.Request(f"{base_url}/v1/quota/reserve", data=res_data_b, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=3) as resp:
        require(resp.status == 200, "reserve all 100 after auto-rollback failed")
        data = json.loads(resp.read().decode())
        require(data.get("reserved") is True, "expected reserved=True for 100 quota")
        require(data.get("remaining") == 0, f"expected remaining 0, got {data.get('remaining')}")
        res_id_b = data.get("reservation_id")

    # 6. Worker B commits 80 (refunds 20)
    commit_data_b = json.dumps({"key": key, "reservation_id": res_id_b, "actual_amount": 80}).encode()
    req = urllib.request.Request(f"{base_url}/v1/quota/commit", data=commit_data_b, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=3) as resp:
        require(resp.status == 200, "commit 80 failed")
        data = json.loads(resp.read().decode())
        require(data.get("committed") is True, "commit flag false")
        require(data.get("actual_amount") == 80, "actual_amount mismatch")
        require(data.get("refunded") == 20, "refunded mismatch")
        require(data.get("remaining") == 20, f"expected remaining 20, got {data.get('remaining')}")

    print("Auto-rollback on timeout test passed successfully!")

finally:
    process.terminate()
    process.communicate(timeout=3)
