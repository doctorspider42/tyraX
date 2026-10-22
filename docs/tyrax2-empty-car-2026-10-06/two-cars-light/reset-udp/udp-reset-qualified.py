"""Private reset experiment: one UDP request, no TCP setup or readiness claim."""
from pathlib import Path
import argparse, datetime, hashlib, json, socket, struct

p = argparse.ArgumentParser()
p.add_argument('--out', type=Path, required=True)
p.add_argument('--host', default='192.168.100.150')
a = p.parse_args()
assert not a.out.exists(), 'Refuse evidence overwrite'
packet = struct.pack('!IH', 0xBABE0201, 6)
assert packet == bytes.fromhex('BA BE 02 01 00 06')
record = dict(status='REQUEST_NOT_SENT', destination=[a.host,18194], packetHex=packet.hex(),
              tcpUsed=False, resetCompletionQualified=False, wireDeliveryQualified=False,
              helperSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
a.out.parent.mkdir(parents=True, exist_ok=True)
try:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(2)
        record['acceptedBytes'] = s.sendto(packet, (a.host,18194))
        record['localAddress'] = s.getsockname()
        assert record['acceptedBytes'] == 6
        record['status'] = 'UDP_REQUEST_ACCEPTED_BY_HOST_SOCKET_ONLY'
finally:
    a.out.write_bytes((json.dumps(record, indent=2)+'\n').encode())
print(record['status'])
