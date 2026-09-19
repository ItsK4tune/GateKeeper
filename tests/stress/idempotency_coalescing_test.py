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

    base_url = f"http://127.0.0.1:{http_port}"
    key = f"coalesce_order_{int(time.time()*1000)}"
    req_hash = "hash_unique_12345"

    payload = json.dumps({
        "key": key,
        "request_hash": req_hash,
        "ttl_ms": 60000,
    }).encode()

    def send_begin(_):
        req = urllib.request.Request(
            f"{base_url}/v1/idempotency/begin",
            data=payload,
            headers={"Content-Type": "application/json"},
            method="POST"
        )
        try:
            with urllib.request.urlopen(req, timeout=5) as resp:
                return resp.status, json.loads(resp.read().decode())
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read().decode())

    # Send 100 concurrent requests with exact same key and hash
    num_requests = 100
    with concurrent.futures.ThreadPoolExecutor(max_workers=20) as executor:
        results = list(executor.map(send_begin, range(num_requests)))

    execute_count = 0
    park_count = 0
    owner_token = None

    for status, data in results:
        require(status == 200, f"expected status 200, got {status}")
        action = data.get("action")
        if action == "EXECUTE":
            execute_count += 1
            owner_token = data.get("owner_token")
        elif action == "PARK":
            park_count += 1
        else:
            require(False, f"unexpected action {action}")

    require(execute_count == 1, f"expected exactly 1 EXECUTE, got {execute_count}")
    require(park_count == num_requests - 1, f"expected {num_requests - 1} PARK, got {park_count}")
    require(bool(owner_token), "owner_token must be non-empty")

    # Complete the operation
    complete_payload = json.dumps({
        "key": key,
        "owner_token": owner_token,
        "response_code": 200,
        "response_body": "{\"status\":\"success\",\"items\":[1,2,3]}",
    }).encode()

    req = urllib.request.Request(
        f"{base_url}/v1/idempotency/complete",
        data=complete_payload,
        headers={"Content-Type": "application/json"},
        method="POST"
    )
    with urllib.request.urlopen(req, timeout=5) as resp:
        require(resp.status == 200, "complete status != 200")
        cdata = json.loads(resp.read().decode())
        require(cdata.get("completed") is True, "expected completed: True")

    # Subsequent request gets REPLAY
    status, rdata = send_begin(0)
    require(status == 200, f"replay status != 200: {status}")
    require(rdata.get("action") == "REPLAY", f"expected action REPLAY, got {rdata.get('action')}")
    require(rdata.get("response_code") == 200, "expected response_code 200")
    require(rdata.get("response_body") == "{\"status\":\"success\",\"items\":[1,2,3]}", "response body mismatch")

    print(f"Idempotency coalescing test passed ({num_requests} requests: 1 EXECUTE, {park_count} PARK, subsequent REPLAY)!")

finally:
    process.terminate()
    process.communicate(timeout=3)
