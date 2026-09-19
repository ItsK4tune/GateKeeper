import socket
import struct
import subprocess
import sys
import threading


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run_error(arguments, expected):
    result = subprocess.run(arguments, text=True, capture_output=True, timeout=10)
    output = result.stdout + result.stderr
    require(result.returncode != 0, "expected failure: " + output)
    for text in expected:
        require(text in output, "missing " + text + " in: " + output)
    return output


def peer_error(gate, mode, expected):
    failures = []
    release = threading.Event()
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(5)
        port = listener.getsockname()[1]

        def serve():
            try:
                connection, _ = listener.accept()
                with connection:
                    connection.settimeout(5)
                    data = b""
                    while len(data) < 24:
                        chunk = connection.recv(24 - len(data))
                        require(chunk, "request header EOF")
                        data += chunk
                    remaining = struct.unpack(">I", data[20:24])[0]
                    while remaining:
                        chunk = connection.recv(remaining)
                        require(chunk, "request body EOF")
                        remaining -= len(chunk)
                    if mode == "closed":
                        return
                    elif mode == "partial":
                        connection.sendall(b"\x47\x4B")
                    elif mode == "oversized":
                        hdr = struct.pack(">HBBBBBBQII", 0x474B, 2, 8, 2, 0, 0, 0, 1, 1, 16777217)
                        connection.sendall(hdr)
                    elif mode == "timeout":
                        release.wait(8)
            except Exception as error:
                failures.append(error)

        worker = threading.Thread(target=serve)
        worker.start()
        try:
            run_error([gate, "-p", str(port), "PING"], [expected, str(port)])
        finally:
            release.set()
            worker.join(6)
        require(not worker.is_alive() and not failures, str(failures))


gate, server = sys.argv[1:]
with socket.socket() as occupied:
    occupied.bind(("0.0.0.0", 0))
    occupied.listen(1)
    port = occupied.getsockname()[1]
    run_error([server, str(port)], ["ADDRESS_IN_USE", str(port), "ss -ltnp"])

with socket.socket() as reserved:
    reserved.bind(("127.0.0.1", 0))
    port = reserved.getsockname()[1]
    run_error([gate, "-p", str(port), "PING"], ["CONNECTION_REFUSED", str(port), "gatekeeper is running"])
    output = run_error([gate, "-p", str(port)], ["CONNECTION_REFUSED", str(port), "gatekeeper is running"])
    require("gate> " not in output, "REPL displayed a prompt before reporting an unavailable server")

run_error([gate, "-h", "bad host!", "PING"], ["DNS_ERROR", "bad host!"])
run_error([gate, "-h", "", "PING"], ["INVALID_HOST"])
for port in ("0", "-1", "65536", "abc", "12junk"):
    run_error([gate, "-p", port, "PING"], ["INVALID_PORT"])
run_error([gate, "-p"], ["INVALID_OPTION", "missing value"])
run_error([server, "0"], ["INVALID_PORT"])
run_error([gate, "pin"], ["UNKNOWN_COMMAND", "Did you mean: PING?"])
peer_error(gate, "closed", "CONNECTION_CLOSED")
peer_error(gate, "partial", "CONNECTION_CLOSED")
peer_error(gate, "oversized", "INVALID_FRAME")
peer_error(gate, "timeout", "TIMEOUT")
print("Network error and recovery tests passed")
