import json
import shutil
import socket
import subprocess
import sys
import tempfile
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
data_dir = tempfile.mkdtemp(prefix="gk_aof_rec_")

try:
    # Phase 1: Start server with AOF, perform operations, then terminate
    port1 = get_free_port()
    http_port1 = get_free_port()
    while http_port1 == port1:
        http_port1 = get_free_port()

    proc1 = subprocess.Popen(
        [server_bin, "--port", str(port1), "--http-port", str(http_port1),
         "--persistence", "aof", "--data-dir", data_dir, "--fsync", "always"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    try:
        deadline = time.monotonic() + 5
        while True:
            require(proc1.poll() is None, "server 1 exited before listening")
            try:
                with socket.create_connection(("127.0.0.1", http_port1), timeout=0.2):
                    break
            except OSError:
                require(time.monotonic() < deadline, "server 1 did not start listening")
                time.sleep(0.02)

        base1 = f"http://127.0.0.1:{http_port1}"

        # 1. Idempotency record
        idem_payload = json.dumps({
            "key": "order_recover_key",
            "request_hash": "hash_rec_abc",
            "ttl_ms": 60000,
        }).encode()

        req = urllib.request.Request(f"{base1}/v1/idempotency/begin", data=idem_payload, headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=3) as resp:
            data = json.loads(resp.read().decode())
            require(data.get("action") == "EXECUTE", "expected action EXECUTE")
            token = data.get("owner_token")

        comp_payload = json.dumps({
            "key": "order_recover_key",
            "owner_token": token,
            "response_code": 201,
            "response_body": "{\"id\":\"order_999\",\"status\":\"paid\"}",
        }).encode()
        req = urllib.request.Request(f"{base1}/v1/idempotency/complete", data=comp_payload, headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=3) as resp:
            require(resp.status == 200, "complete status != 200")

        # 2. Quota record
        init_payload = json.dumps({"key": "quota_recover_key", "quota": 500}).encode()
        req = urllib.request.Request(f"{base1}/v1/quota/init", data=init_payload, headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=3) as resp:
            require(resp.status == 200, "quota init failed")

        res_payload = json.dumps({"key": "quota_recover_key", "amount": 100, "ttl_ms": 60000}).encode()
        req = urllib.request.Request(f"{base1}/v1/quota/reserve", data=res_payload, headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=3) as resp:
            data = json.loads(resp.read().decode())
            require(data.get("reserved") is True, "reserve failed")
            require(data.get("remaining") == 400, "remaining != 400")

    finally:
        # Hard terminate (simulate crash)
        proc1.kill()
        proc1.wait(timeout=3)

    time.sleep(0.1)

    # Phase 2: Start new server instance with same data_dir and verify recovery
    port2 = get_free_port()
    http_port2 = get_free_port()
    while http_port2 == port2:
        http_port2 = get_free_port()

    proc2 = subprocess.Popen(
        [server_bin, "--port", str(port2), "--http-port", str(http_port2),
         "--persistence", "aof", "--data-dir", data_dir, "--fsync", "always"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    try:
        deadline = time.monotonic() + 5
        while True:
            require(proc2.poll() is None, "server 2 exited before listening")
            try:
                with socket.create_connection(("127.0.0.1", http_port2), timeout=0.2):
                    break
            except OSError:
                require(time.monotonic() < deadline, "server 2 did not start listening")
                time.sleep(0.02)

        base2 = f"http://127.0.0.1:{http_port2}"

        # Verify Idempotency recovered
        req = urllib.request.Request(f"{base2}/v1/idempotency/begin", data=idem_payload, headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=3) as resp:
            require(resp.status == 200, "server 2 begin != 200")
            data = json.loads(resp.read().decode())
            require(data.get("action") == "REPLAY", f"expected REPLAY, got {data.get('action')}")
            require(data.get("response_code") == 201, f"expected code 201, got {data.get('response_code')}")
            require(data.get("response_body") == "{\"id\":\"order_999\",\"status\":\"paid\"}", "body mismatch")

        # Verify Quota recovered: remaining 400 can be reserved
        res2_payload = json.dumps({"key": "quota_recover_key", "amount": 400, "ttl_ms": 60000}).encode()
        req = urllib.request.Request(f"{base2}/v1/quota/reserve", data=res2_payload, headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=3) as resp:
            data = json.loads(resp.read().decode())
            require(data.get("reserved") is True, "reserve 400 failed")
            require(data.get("remaining") == 0, f"remaining != 0, got {data.get('remaining')}")

        print("AOF crash recovery test passed successfully!")

    finally:
        proc2.terminate()
        proc2.communicate(timeout=3)

finally:
    shutil.rmtree(data_dir, ignore_errors=True)
