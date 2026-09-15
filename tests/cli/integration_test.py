import json
import socket
import struct
import subprocess
import sys
import threading


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def read_all(connection, length):
    data = b""
    while len(data) < length:
        chunk = connection.recv(length - len(data))
        if not chunk:
            raise RuntimeError("unexpected EOF")
        data += chunk
    return data


def round_trip(executable, repl):
    requests = []
    failures = []
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(5)

    def serve():
        try:
            connection, _ = listener.accept()
            with connection:
                connection.settimeout(5)
                length = struct.unpack(">I", read_all(connection, 4))[0]
                requests.append(json.loads(read_all(connection, length)))
                payload = json.dumps({"id": "gate-1", "ok": True, "result": {"pong": True}}).encode()
                packet = struct.pack(">I", len(payload)) + payload
                for byte in packet:
                    connection.sendall(bytes([byte]))
                require(connection.recv(1) == b"", "CLI sent an extra request after PING")
        except Exception as error:
            failures.append(error)

    worker = threading.Thread(target=serve)
    worker.start()
    try:
        args = [executable, "-h", "127.0.0.1", "-p", str(listener.getsockname()[1])]
        if not repl:
            args.append("pInG")
        result = subprocess.run(args, input="Help\npInG\nqUiT\nPING\n" if repl else None,
                                text=True, capture_output=True, timeout=8)
        require(result.returncode == 0, result.stderr or result.stdout)
        require('"pong": true' in result.stdout, "PING response missing")
    finally:
        worker.join(6)
        listener.close()
    require(not worker.is_alive(), "server thread stuck")
    require(not failures, str(failures))
    require(requests == [{"id": "gate-1", "op": "PING", "body": {}}], "unexpected requests")


executable = sys.argv[1]
for command in ("Help", "HElP", "hElp", "qUiT", "eXiT"):
    result = subprocess.run([executable, "-h", "invalid.invalid", command],
                            text=True, capture_output=True, timeout=3)
    require(result.returncode == 0, "local command tried to connect: " + command)
for command in ("PING extra", "QUIT extra", "unknown"):
    result = subprocess.run([executable, command], text=True, capture_output=True, timeout=3)
    require(result.returncode != 0, "invalid command accepted: " + command)
round_trip(executable, False)
round_trip(executable, True)
print("CLI executable integration tests passed")
