import json
import socket
import subprocess
import sys
import time

def require(cond, msg):
    if not cond:
        raise RuntimeError(msg)

def get_free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]

def make_gkwp2_frame(req_id, stream_id, payload_bytes):
    magic = b'GK'
    version = (2).to_bytes(1, 'big')
    flags = (0).to_bytes(1, 'big')
    msg_type = (1).to_bytes(1, 'big')
    reserved = b'\x00\x00\x00'
    rid = req_id.to_bytes(8, 'big')
    sid = stream_id.to_bytes(4, 'big')
    plen = len(payload_bytes).to_bytes(4, 'big')
    return magic + version + flags + msg_type + reserved + rid + sid + plen + payload_bytes

def read_gkwp2_frame(sock):
    header = sock.recv(24)
    if len(header) < 24:
        raise RuntimeError("Incomplete header")
    plen = int.from_bytes(header[20:24], 'big')
    payload = b''
    while len(payload) < plen:
        chunk = sock.recv(plen - len(payload))
        if not chunk:
            break
        payload += chunk
    return header, payload

# Mock Downstream Storage with Fencing Token Check (Martin Kleppmann Model)
class MockDatabase:
    def __init__(self):
        self.last_fencing_token = 0
        self.processed_invoices = []

    def commit_invoice(self, invoice_id, amount, fencing_token):
        if fencing_token < self.last_fencing_token:
            # Reject stale write from paused worker!
            return False, f"REJECTED: fencing_token {fencing_token} < last_fencing_token {self.last_fencing_token}"
        self.last_fencing_token = fencing_token
        self.processed_invoices.append((invoice_id, amount, fencing_token))
        return True, "COMMITTED"

db = MockDatabase()

# Simulation:
# Worker 1 gets lock with fencing token 1001
# Worker 1 pauses
# Worker 2 gets lock with fencing token 1002, commits invoice $500
ok, msg = db.commit_invoice("INV-001", 500, 1002)
require(ok, "Worker 2 commit should succeed")

# Worker 1 wakes up and tries to commit invoice $100 with stale fencing token 1001
ok_stale, msg_stale = db.commit_invoice("INV-001", 100, 1001)
require(not ok_stale, "Worker 1 stale write MUST be rejected by database")
print(f"Downstream storage successfully rejected stale write: {msg_stale}")
require(len(db.processed_invoices) == 1, "Only Worker 2 invoice must be committed")
require(db.processed_invoices[0][1] == 500, "Invoice amount must be exactly $500 from Worker 2")

print("Martin Kleppmann fencing token simulation test passed!")
