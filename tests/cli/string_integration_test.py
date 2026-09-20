import json
import socket
import struct
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
    return result.stdout.strip()
try:
    for _ in range(100):
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=.1):
                break
        except OSError:
            assert process.poll() is None
            time.sleep(.02)
    assert run("SET name duong") == "OK"
    with socket.create_connection(("127.0.0.1", port), timeout=2) as wire:
        def read_exact(count):
            data = b""
            while len(data) < count:
                part = wire.recv(count - len(data))
                assert part
                data += part
            return data
        def request_set(key, value, cond=0, ttl_ms=0):
            k_bytes = key.encode("utf-8")
            v_bytes = value.encode("utf-8")
            body = (struct.pack(">HH", 0x0010, len(k_bytes)) + k_bytes +
                    struct.pack(">I", len(v_bytes)) + v_bytes +
                    struct.pack(">BQ", cond, ttl_ms))
            header = struct.pack(">HBBBBBBQII", 0x474B, 2, 0, 1, 0, 0, 0, 1, 1, len(body))
            wire.sendall(header + body)
            resp_hdr = read_exact(24)
            magic, ver, flags, msg_type, _, _, _, req_id, stream_id, length = struct.unpack(">HBBBBBBQII", resp_hdr)
            assert magic == 0x474B
            resp_body = read_exact(length)
            return resp_body[0] # BinaryStatus: 0 = Ok, 4 = Conflict, 1 = Error
        status = request_set("unicode", 'Việt Nam " \\ \n')
        assert status == 0
    assert run("GET unicode") == json.dumps('Việt Nam " \\ \n', ensure_ascii=False)
    assert run("GET name") == '"duong"'
    assert run("SET name other NX") == "(nil)"
    assert run("GET name") == '"duong"'
    assert run("SET missing x XX") == "(nil)"
    assert run('SET name "hello world" xx') == "OK"
    assert run("GET name") == '"hello world"'
    assert run('SET name ""') == "OK"
    assert run("GET name") == '""'
    for command in ["SET", "SET name", "GET", "GET name extra", "SET name v BAD", 'SET name "unterminated']:
        run(command, False)
    result = subprocess.run([cli, "-p", str(port)], input="SET repl value\nGET repl\nQUIT\n",
                            text=True, capture_output=True, timeout=5)
    assert result.returncode == 0 and '"value"' in result.stdout, result.stdout
    print("Step 4 CLI end-to-end passed")
finally:
    process.terminate()
    process.wait(timeout=5)
