"""Exercise real SSDP/SOAP, HTTP demux/decode, audio, pause, seek and stop."""
import functools
import http.server
import os
from pathlib import Path
import socket
import subprocess
import sys
import threading
import time
import urllib.request
import urllib.error
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape

exe, fixture = sys.argv[1:]
class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_NOTIFY(self):
        self.rfile.read(int(self.headers.get('Content-Length', '0')))
        self.send_response(200); self.end_headers()

server = http.server.ThreadingHTTPServer(('0.0.0.0', 0), functools.partial(Handler, directory=str(Path(fixture).parent)))
threading.Thread(target=server.serve_forever, daemon=True).start()
env = dict(os.environ, SDL_AUDIODRIVER='dummy')
process = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
try:
    location = process.stdout.readline().strip()
    assert location.startswith('http://'), 'renderer starts on LAN'
    root = ET.fromstring(urllib.request.urlopen(location, timeout=3).read())
    base = location.rsplit('/', 1)[0]
    ns = {'d': 'urn:schemas-upnp-org:device-1-0'}
    assert root.find('.//d:friendlyName', ns).text == 'MirrorPSX - Video'
    for node in root.findall('.//d:service', ns):
        ET.fromstring(urllib.request.urlopen(base + node.find('d:SCPDURL', ns).text, timeout=3).read())
    host = base.split('//')[1].split(':')[0]
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((host, 0)); sock.settimeout(3)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(host))
        sock.sendto(b'M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: "ssdp:discover"\r\nMX: 1\r\nST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n\r\n', ('239.255.255.250', 1900))
        deadline = time.monotonic()+3
        while time.monotonic()<deadline:
            if location.encode() in sock.recv(8192): break
        else: raise AssertionError('SSDP discovery')
    def soap(action, values=None, service='AVTransport', expected=200):
        values = dict(values or {})
        if service != 'ConnectionManager': values.setdefault('InstanceID', '0')
        urn = 'urn:schemas-upnp-org:service:' + service + ':1'
        body = ('<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><u:' + action + ' xmlns:u="' + urn + '">' + ''.join('<'+k+'>'+escape(str(v))+'</'+k+'>' for k,v in values.items()) + '</u:' + action + '></s:Body></s:Envelope>').encode()
        request = urllib.request.Request(base+'/'+service+'/control', body, {'Content-Type': 'text/xml', 'SOAPAction': '"'+urn+'#'+action+'"'})
        try:
            with urllib.request.urlopen(request, timeout=4) as response:
                code, text = response.status, response.read()
        except urllib.error.HTTPError as e: code, text = e.code, e.read()
        assert code == expected, (action, code, text)
        return ET.fromstring(text)
    def snapshot():
        process.stdin.write('snapshot\n'); process.stdin.flush()
        fields = process.stdout.readline().strip().split()
        assert len(fields)==7, fields
        return int(fields[0]), int(fields[1]), int(fields[2]), fields[3], float(fields[4]), float(fields[5]), int(fields[6])
    callback = f'<http://{host}:{server.server_port}/events>'
    request = urllib.request.Request(base+'/AVTransport/event', method='SUBSCRIBE', headers={'CALLBACK': callback, 'NT': 'upnp:event', 'TIMEOUT': 'Second-1800'})
    with urllib.request.urlopen(request, timeout=4) as response:
        assert response.headers['SID']
        sid = response.headers['SID']
    soap('SetAVTransportURI', {'CurrentURI': 'file:///C:/Windows/win.ini', 'CurrentURIMetaData': ''}, expected=500)
    uri = f'http://127.0.0.1:{server.server_port}/{Path(fixture).name}'
    soap('SetAVTransportURI', {'CurrentURI': uri, 'CurrentURIMetaData': '<DIDL-Lite><item><title>Test video</title></item></DIDL-Lite>'})
    soap('Play', {'Speed': '1'})
    deadline = time.monotonic()+8
    while time.monotonic()<deadline:
        s = snapshot()
        if s[0]>5 and s[1] and s[5]>3: break
        assert not s[6], ('playback error', s)
        time.sleep(.1)
    else: raise AssertionError('decoded video with duration')
    soap('Pause'); time.sleep(.2)
    paused = snapshot(); time.sleep(.25); later = snapshot()
    assert paused[2] and paused[3]=='PAUSED_PLAYBACK' and paused[0]==later[0], (paused, later)
    soap('SetVolume', {'Channel':'Master','DesiredVolume':'40'}, service='RenderingControl')
    soap('SetMute', {'Channel':'Master','DesiredMute':'1'}, service='RenderingControl')
    soap('Seek', {'Unit':'REL_TIME','Target':'00:00:02'})
    soap('Play', {'Speed':'1'}); time.sleep(.5)
    assert snapshot()[4]>=1.9, 'seek reports position'
    soap('GetPositionInfo'); soap('GetTransportInfo'); soap('GetProtocolInfo', service='ConnectionManager')
    soap('GetTransportInfo', {'InstanceID':'1'}, expected=500)
    soap('Stop'); time.sleep(.2)
    assert snapshot()[1]==0, 'stop clears video state'
    request = urllib.request.Request(base+'/AVTransport/event', method='UNSUBSCRIBE', headers={'SID': sid})
    urllib.request.urlopen(request, timeout=4).close()
    print('SSDP, descriptions, SOAP, subscription, URL validation, audio/video, pause, seek, stop: passed')
finally:
    if process.poll() is None:
        process.stdin.write('quit\n'); process.stdin.flush()
    try: _, errors = process.communicate(timeout=10)
    except subprocess.TimeoutExpired: process.kill(); _, errors = process.communicate(); raise
    server.shutdown(); server.server_close()
    if process.returncode: raise RuntimeError(errors)
