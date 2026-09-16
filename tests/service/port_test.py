import socket
import subprocess
import sys
import time


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


server, gate = sys.argv[1:]
help_result = subprocess.run([server, "--help"], capture_output=True, text=True, timeout=3)
require(help_result.returncode == 0 and "63779" in help_result.stdout and "--port" in help_result.stdout,
        "help did not report port options")
for arguments, code in (
    (["-p"], "INVALID_OPTION"),
    (["--port"], "INVALID_OPTION"),
    (["--unknown"], "INVALID_OPTION"),
    (["-p", "64000", "--port", "64001"], "INVALID_OPTION"),
    *[(["--port", value], "INVALID_PORT") for value in ("0", "-1", "65536", "abc", "12x", "", "9999999999999")],
):
    result = subprocess.run([server, *arguments], capture_output=True, text=True, timeout=3)
    require(result.returncode != 0 and code in result.stderr, "bad option accepted: " + repr(arguments))

with socket.socket() as occupied:
    occupied.bind(("0.0.0.0", 0))
    occupied.listen(1)
    port = occupied.getsockname()[1]
    result = subprocess.run([server, "--port", str(port)], capture_output=True, text=True, timeout=3)
    require(result.returncode != 0 and "ADDRESS_IN_USE" in result.stderr and str(port) in result.stderr,
            "occupied custom port not reported")

for flag in ("-p", "--port"):
    with socket.socket() as temporary:
        temporary.bind(("127.0.0.1", 0))
        port = temporary.getsockname()[1]
    process = subprocess.Popen([server, flag, str(port)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        deadline = time.monotonic() + 5
        while True:
            require(process.poll() is None, "server exited before listening")
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                    break
            except OSError:
                require(time.monotonic() < deadline, "server did not open custom port")
                time.sleep(0.02)
        result = subprocess.run([gate, "-p", str(port), "PING"], capture_output=True, text=True, timeout=3)
        require(result.returncode == 0 and '"pong":true' in result.stdout,
                "PING on custom port failed: " + result.stdout + result.stderr)
    finally:
        process.terminate()
        process.communicate(timeout=3)
print("Service port tests passed")
