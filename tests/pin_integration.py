# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent SRP client over real RTSP sockets; no Apple hardware required."""
import hashlib, os, pathlib, plistlib, re, secrets, socket, subprocess, sys, time
root = pathlib.Path(__file__).resolve().parents[1]
constants = (root / 'third_party/uxplay/lib/srp.c').read_text()
group = constants.split('{ /* 2048 */')[1].split('},')[0]
N = int(''.join(re.findall(r'"([A-F0-9]+)"', group)[:-1]), 16)
g = 2
def b(n): return n.to_bytes(max(1, (n.bit_length()+7)//8), 'big')
def H(*parts): return hashlib.sha1(b''.join(parts)).digest()
def h(*parts): return int.from_bytes(H(*parts), 'big')
def pad(n): return n.to_bytes(256, 'big')
k = h(pad(N), pad(g))
run = pathlib.Path(sys.argv[1]).resolve()
work = root / ('build/pin-short-test' if '--short-encoding' in sys.argv else 'build/pin-test')
work.mkdir(exist_ok=True)
with (work/'server.log').open('w') as log:
 p = subprocess.Popen([str(run)], cwd=work, stdin=subprocess.PIPE,
                      stdout=subprocess.PIPE, stderr=log, text=True)
 try:
  port = int(p.stdout.readline())
  def connect(): return socket.create_connection(('127.0.0.1', port), timeout=2)
  def request(s, path, body=None):
   data = plistlib.dumps(body, fmt=plistlib.FMT_BINARY) if body is not None else b''
   msg = (f'POST /{path} RTSP/1.0\r\nCSeq: 0\r\nContent-Length: {len(data)}\r\n'
          'Content-Type: application/x-apple-binary-plist\r\n\r\n').encode()+data
   s.sendall(msg)
   received = b''
   while b'\r\n\r\n' not in received:
    chunk = s.recv(4096)
    assert chunk, 'connection closed before response headers'
    received += chunk
   headers, payload = received.split(b'\r\n\r\n',1)
   length = re.search(rb'Content-Length:\s*(\d+)', headers, re.I)
   length = int(length[1]) if length else 0
   while len(payload) < length:
    chunk = s.recv(4096)
    assert chunk, 'response truncated'
    payload += chunk
   return int(headers.split()[1]), plistlib.loads(payload) if length else None, headers
  def get_pin():
   p.stdin.write('pin\n'); p.stdin.flush()
   return p.stdout.readline().strip()
  def attempt(pin, expected):
   with connect() as s:
    status, response, _ = request(s, 'pair-setup-pin', {'method':'pin','user':'12:15:17:A2:ED:7E'})
    assert status == 200, f'SRP challenge setup: expected 200, got {status}'
    if '--short-encoding' in sys.argv:
     assert response['salt'] == b'\0'*15+b'\x01', 'short salt must be left-padded to its wire width'
    salt = b(int.from_bytes(response['salt'],'big')); B = int.from_bytes(response['pk'],'big')
    username = b'12:15:17:A2:ED:7E'
    a = int.from_bytes(secrets.token_bytes(32),'big'); A = pow(g,a,N)
    x = h(salt,H(username,b':',pin.encode())); u = h(pad(A),pad(B))
    S = pow((B-k*pow(g,x,N))%N, a+u*x,N)
    K = H(b(S),b'\0\0\0\0')+H(b(S),b'\0\0\0\1')
    xor = bytes(x^y for x,y in zip(H(b(N)),H(b(g))))
    proof = H(xor,H(username),salt,b(A),b(B),K)
    status, response, _ = request(s,'pair-setup-pin',{'pk':pad(A),'proof':proof})
    assert status == expected, f'Expected {expected}, got {status}'
    if expected == 200:
     assert response['proof'] == H(b(A),proof,K), 'server proof length/content incorrect'
  with connect() as s:
   start = time.monotonic()
   status, _, headers = request(s,'pair-pin-start')
   latency = (time.monotonic()-start)*1000
   assert status == 200
   if '--baseline' not in sys.argv: assert b'Content-Length: 0' in headers, 'empty PIN-start response needs explicit framing'
  pin = get_pin(); assert len(pin)==4
  attempt(pin,200)
  attempt(str((int(pin)+1)%10000).zfill(4),470)
  attempt(pin,200) # same visible challenge remains valid after failure/reconnect
  print(f'PIN start response: {latency:.1f} ms; correct PIN, bad PIN rejection, retry and server proof passed')
 finally:
  p.stdin.write('quit\n'); p.stdin.flush()
  try: p.wait(timeout=5)
  except subprocess.TimeoutExpired: p.kill(); p.wait()
