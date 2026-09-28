#!/usr/bin/env python3
"""
ke_bridge.py - tiny receiver for recordings from the ke-body board.

    python ke_bridge.py            # listens on 0.0.0.0:8770
    python ke_bridge.py --port 8770 --inbox inbox

The board POSTs a WAV (Content-Type: audio/wav) to /hear after every
push-to-talk. Each one is saved as inbox/YYYYMMDD-HHMMSS.wav and the file
name is printed. Speech-to-text is NOT done here; hook it up separately.

Standard library only.
"""
import argparse
import datetime
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))


class Handler(BaseHTTPRequestHandler):
    inbox = os.path.join(HERE, "inbox")

    def _reply(self, code, text):
        body = text.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        self._reply(200, "ke_bridge ok\n")

    def do_POST(self):
        if self.path.rstrip("/") != "/hear":
            self._reply(404, "unknown path (use POST /hear)\n")
            return
        length = int(self.headers.get("Content-Length", "0"))
        data = self.rfile.read(length)
        if len(data) < 44 or data[:4] != b"RIFF":
            self._reply(400, "not a wav\n")
            return
        os.makedirs(self.inbox, exist_ok=True)
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        path = os.path.join(self.inbox, stamp + ".wav")
        n = 1
        while os.path.exists(path):        # two recordings within one second
            n += 1
            path = os.path.join(self.inbox, f"{stamp}-{n}.wav")
        with open(path, "wb") as f:
            f.write(data)
        secs = max(0, len(data) - 44) / (16000 * 2)
        print(f"{path}  ({len(data)} bytes, ~{secs:.1f}s from {self.client_address[0]})", flush=True)
        self._reply(200, "ok\n")

    def log_message(self, fmt, *args):   # keep the console to one line per file
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8770)
    ap.add_argument("--inbox", default=os.path.join(HERE, "inbox"))
    args = ap.parse_args()
    Handler.inbox = os.path.abspath(args.inbox)
    os.makedirs(Handler.inbox, exist_ok=True)
    srv = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    print(f"ke_bridge listening on 0.0.0.0:{args.port}, saving to {Handler.inbox}", flush=True)
    print("on the board:  server http://<this-pc-ip>:%d/hear" % args.port, flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
