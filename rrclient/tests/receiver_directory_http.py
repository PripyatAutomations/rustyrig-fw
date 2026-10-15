import http.server
import json
import os
import subprocess
import sys
import threading

requests = []
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass
    def do_GET(self):
        requests.append(dict(self.headers))
        assert self.path == '/receivers.json'
        assert self.headers['User-Agent'].startswith('RustyRig/')
        step = len(requests)
        if step == 1:
            self.send_response(200)
            self.send_header('ETag', '"fixture-v1"')
            self.end_headers()
            self.wfile.write(json.dumps({'schema': 1, 'receivers': [{'url': 'http://do-not-contact.invalid:8073/', 'name': 'Fixture receiver', 'ext_api': 4}]}).encode())
        elif step == 2:
            assert self.headers.get('If-None-Match') == '"fixture-v1"'
            self.send_response(304)
            self.end_headers()
        elif step == 3:
            self.send_response(429)
            self.send_header('Retry-After', '7200')
            self.end_headers()
        else:
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b'<html>directory unavailable</html>')

with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
    threading.Thread(target=server.serve_forever, daemon=True).start()
    subprocess.run([sys.argv[1], sys.argv[2], f'http://127.0.0.1:{server.server_port}/receivers.json'], check=True)
    server.shutdown()
assert len(requests) == 4, requests
print('PASS: one mirror request, no receiver probes, conditional GET, restart cooldown, Retry-After and stale fallback')
