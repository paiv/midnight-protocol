#!/usr/bin/env python
import json
import struct
import subprocess
import sys
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler


def player_move(state):
    data = struct.pack('<B', state['currentPlayer'])
    data += struct.pack('<25B', *state['board'])
    data += struct.pack('<5B', *state['progs'])
    p = subprocess.run('./brute', stdout=subprocess.PIPE, input=data, stderr=sys.stderr.buffer)
    if p.returncode:
        print(p.stderr.decode(), file=sys.stderr)
        return
    v,fro,to,pid = p.stdout
    move = [fro, to, pid]
    return move


class GameHttpHandler (BaseHTTPRequestHandler):
    def do_OPTIONS(self, *args):
        self.send_response(204)
        self.send_header('Allow', 'OPTIONS, POST')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'OPTIONS, POST')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()


    def do_POST(self, *args):
        n = self.headers.get('Content-Length')
        n = int(n)
        data = self.rfile.read(n)

        game_state = json.loads(data)
        move = player_move(game_state)
        self.log_message(f'player move {move}')
        data = json.dumps(move, separators=',:')
        data = data.encode()

        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', len(data))
        self.end_headers()
        self.wfile.write(data)


def main():
    server_address = ('', 8001)
    httpd = ThreadingHTTPServer(server_address, GameHttpHandler)
    httpd.serve_forever()


if __name__ == '__main__':
    main()
