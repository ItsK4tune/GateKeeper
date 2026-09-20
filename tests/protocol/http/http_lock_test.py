import json
import socket
import subprocess
import sys
import time
import urllib.request
import urllib.error

def require(cond, msg):
    if not cond:
        raise RuntimeError(msg)

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
    res = f"http:lock:{int(time.time()*1000)}"

    # 1. Acquire
    acq_payload = json.dumps({"resource": res, "ttl_ms": 60000}).encode()
    req = urllib.request.Request(f"{base_url}/v1/lock/acquire", data=acq_payload, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=3) as resp:
        require(resp.status == 200, "acquire failed")
        data = json.loads(resp.read().decode())
        require(data.get("acquired") is True, f"expected acquired=true, got {data}")
        token = data.get("owner_token")
        fencing = data.get("fencing_token")
        require(token and fencing > 0, "token and fencing token must be present")

    # 2. Acquire again -> 409 Conflict
    try:
        urllib.request.urlopen(req, timeout=3)
        require(False, "duplicate acquire should return 409")
    except urllib.error.HTTPError as e:
        require(e.code == 409, f"expected 409, got {e.code}")

    # 3. Extend
    ext_payload = json.dumps({"resource": res, "owner_token": token, "ttl_ms": 30000}).encode()
    req_ext = urllib.request.Request(f"{base_url}/v1/lock/extend", data=ext_payload, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req_ext, timeout=3) as resp:
        require(resp.status == 200, "extend failed")
        data = json.loads(resp.read().decode())
        require(data.get("extended") is True and data.get("fencing_token") == fencing, "extend data mismatch")

    # 4. Release
    rel_payload = json.dumps({"resource": res, "owner_token": token}).encode()
    req_rel = urllib.request.Request(f"{base_url}/v1/lock/release", data=rel_payload, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req_rel, timeout=3) as resp:
        require(resp.status == 200, "release failed")
        data = json.loads(resp.read().decode())
        require(data.get("released") is True, "expected released=true")

    print("HTTP lock endpoints test passed!")

finally:
    process.terminate()
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        process.kill()
