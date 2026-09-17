import json
import socket
import subprocess
import sys
import time

server, cli = sys.argv[1:]
with socket.socket() as probe:
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
process = subprocess.Popen([server, "-p", str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
def run(command, ok=True):
    result = subprocess.run([cli, "-p", str(port), command], capture_output=True, text=True, timeout=5)
    assert (result.returncode == 0) == ok, (command, result.stdout, result.stderr)
    return json.loads(result.stdout) if ok else result.stdout
try:
    for _ in range(100):
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=.1):
                break
        except OSError:
            assert process.poll() is None
            time.sleep(.02)
    assert run("SET name duong")["result"] == {"stored": True}
    with socket.create_connection(("127.0.0.1", port), timeout=2) as wire:
        def read_exact(count):
            data = b""
            while len(data) < count:
                part = wire.recv(count - len(data))
                assert part
                data += part
            return data
        def request(body):
            packet = json.dumps({"id": 'id"quoted', "op": "SET", "body": body}).encode()
            wire.sendall(len(packet).to_bytes(4, "big") + packet)
            size = int.from_bytes(read_exact(4), "big")
            return json.loads(read_exact(size))
        result = request({"key": "unicode", "value": 'Việt 😀 " \\ \n'})
        assert result["ok"] and result["id"] == 'id"quoted'
        result = request({"key": "unicode", "value": "bad", 'unexpected"field': True})
        assert not result["ok"] and result["error"]["code"] == "INVALID_ARGUMENTS"
    assert run("GET unicode")["result"] == {"value": 'Việt 😀 " \\ \n'}
    assert run("GET name")["result"] == {"value": "duong"}
    assert run("SET name other NX")["result"] == {"stored": False}
    assert run("GET name")["result"] == {"value": "duong"}
    assert run("SET missing x XX")["result"] == {"stored": False}
    assert run('SET name "hello world" xx')["result"] == {"stored": True}
    assert run("GET name")["result"] == {"value": "hello world"}
    assert run('SET name ""')["result"] == {"stored": True}
    assert run("GET name")["result"] == {"value": ""}
    for command in ["SET", "SET name", "GET", "GET name extra", "SET name v BAD", 'SET name "unterminated']:
        run(command, False)
    result = subprocess.run([cli, "-p", str(port)], input="SET repl value\nGET repl\nQUIT\n",
                            text=True, capture_output=True, timeout=5)
    assert result.returncode == 0 and '"value":"value"' in result.stdout, result.stdout
    print("Step 4 CLI end-to-end passed")
finally:
    process.terminate()
    process.wait(timeout=5)
