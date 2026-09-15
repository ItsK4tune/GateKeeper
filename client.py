import socket, struct, json

payload = json.dumps({"id": "req-1", "op": "PING", "body": {}}).encode()
packet = struct.pack(">I", len(payload)) + payload

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(("127.0.0.1", 63779))
s.sendall(packet)

raw_len = s.recv(4)
resp_len = struct.unpack(">I", raw_len)[0]

response = s.recv(resp_len).decode()

print("Response:", response)

s.close()
