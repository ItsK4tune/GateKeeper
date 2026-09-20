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
                hdr = read_all(connection, 24)
                magic, ver, flags, msg_type, _, _, _, req_id, stream_id, length = struct.unpack(">HBBBBBBQII", hdr)
                require(magic == 0x474B, "invalid GKWP/2 magic")
                body = read_all(connection, length)
                opcode = struct.unpack(">H", body[:2])[0]
                requests.append(opcode)
                payload = b"PONG"
                resp_hdr = struct.pack(">HBBBBBBQII", 0x474B, 2, 0, 2, 0, 0, 0, req_id, stream_id, len(payload))
                packet = resp_hdr + payload
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
        require("PONG" in result.stdout, "PING response missing")
    finally:
        worker.join(6)
        listener.close()
    require(not worker.is_alive(), "server thread stuck")
    require(not failures, str(failures))
    require(requests == [0x0001], "unexpected requests")


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
