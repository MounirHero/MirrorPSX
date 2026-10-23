"""Check local DNS-SD replies; this does not substitute an external device test."""
import socket
import struct
import sys
import time

ip = sys.argv[1]
service = '_airplay._tcp.local'
question = b''.join(bytes([len(p)]) + p.encode() for p in service.split('.')) + b'\0'
query = struct.pack('!6H', 0, 0, 1, 0, 0, 0) + question + struct.pack('!2H', 12, 0x8001)

def name(data, offset):
    labels = []
    end = None
    for _ in range(100):
        size = data[offset]
        if size & 0xc0 == 0xc0:
            if end is None:
                end = offset + 2
            offset = ((size & 0x3f) << 8) | data[offset + 1]
            continue
        offset += 1
        if not size:
            return '.'.join(labels), end if end is not None else offset
        labels.append(data[offset:offset+size].decode(errors='replace'))
        offset += size
    raise ValueError('DNS compression loop')

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
    sock.bind((ip, 0))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(ip))
    sock.settimeout(.5)
    sock.sendto(query, ('224.0.0.251', 5353))
    deadline = time.monotonic() + 4
    found = False
    while time.monotonic() < deadline:
        try:
            data, source = sock.recvfrom(65535)
        except socket.timeout:
            continue
        _, flags, qd, an, ns, ar = struct.unpack_from('!6H', data)
        if not flags & 0x8000:
            continue
        offset = 12
        for _ in range(qd):
            _, offset = name(data, offset)
            offset += 4
        records = []
        for _ in range(an+ns+ar):
            owner, offset = name(data, offset)
            kind, cls, ttl, length = struct.unpack_from('!HHIH', data, offset)
            offset += 10
            value = None
            if kind == 12:
                value = name(data, offset)[0]
            elif kind == 33:
                value = f'port={struct.unpack_from("!H",data,offset+4)[0]} host={name(data,offset+6)[0]}'
            elif kind == 1:
                value = socket.inet_ntoa(data[offset:offset+4])
            if value is not None:
                records.append((owner, kind, value))
            offset += length
        if any('mirrorpsx' in (owner + value).lower() for owner, kind, value in records):
            found = True
            print('DNS-SD response from', source[0])
            for record in records:
                print(*record)
    if not found:
        print('No MirrorPSX response to local multicast query')
        sys.exit(1)
