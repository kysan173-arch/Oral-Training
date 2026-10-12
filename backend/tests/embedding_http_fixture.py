"""Offline HTTP transport fixture for API wiring tests; no inference or credentials."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_POST(self):
        if self.path != '/v1/embeddings':
            self.send_error(404)
            return
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        vectors = [{'index': i, 'embedding': [1.0] + [0.0] * (body['dimensions'] - 1)}
                   for i, _ in enumerate(body['input'])]
        payload = json.dumps({'model': body['model'], 'data': vectors}).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--ready-file', required=True, type=Path)
    args = parser.parse_args()
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    args.ready_file.write_text(f'http://127.0.0.1:{server.server_port}/v1', encoding='utf-8')
    server.serve_forever()
