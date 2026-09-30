#!/usr/bin/env python3
"""
ke_bridge.py - tiny receiver for everything the ke-body board sends.

    python ke_bridge.py            # listens on 0.0.0.0:8770
    python ke_bridge.py --port 8770 --inbox inbox

Endpoints (the board derives them from the `server` setting, e.g.
http://<pc-ip>:8770/hear -> /msg, /photo live next to it):

    POST /hear   WAV recording (push-to-talk)  -> inbox/YYYYMMDD-HHMMSS.wav
    POST /msg    UTF-8 text (quick button, shake) -> inbox/YYYYMMDD-HHMMSS.txt
    POST /photo  JPEG she chose to send        -> inbox/photos/YYYYMMDD-HHMMSS.jpg
    POST /ink    PNG of a handwritten sentence  -> inbox/ink/YYYYMMDD-HHMMSS.png
                 (black strokes on white, 96 px high, one cell per character; the board does no recognition -
                 read it with whatever you like, e.g. hand the PNG to a vision model)

Every file name is printed on one line. Speech-to-text / replies are NOT done
here; hook the inbox folder up to whatever you like. Standard library only.
"""
import argparse
import datetime
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))


def unique_path(folder, stamp, ext):
    path = os.path.join(folder, stamp + ext)
    n = 1
    while os.path.exists(path):
        n += 1
        path = os.path.join(folder, f"{stamp}-{n}{ext}")
    return path


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
        path = self.path.rstrip("/")
        length = int(self.headers.get("Content-Length", "0"))
        data = self.rfile.read(length)
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        who = self.client_address[0]

        if path == "/hear":
            if len(data) < 44 or data[:4] != b"RIFF":
                self._reply(400, "not a wav\n")
                return
            os.makedirs(self.inbox, exist_ok=True)
            out = unique_path(self.inbox, stamp, ".wav")
            with open(out, "wb") as f:
                f.write(data)
            secs = max(0, len(data) - 44) / (16000 * 2)
            print(f"{out}  ({len(data)} bytes, ~{secs:.1f}s from {who})", flush=True)
            self._reply(200, "ok\n")
        elif path == "/msg":
            text = data.decode("utf-8", "replace").strip()
            if not text:
                self._reply(400, "empty\n")
                return
            os.makedirs(self.inbox, exist_ok=True)
            out = unique_path(self.inbox, stamp, ".txt")
            with open(out, "w", encoding="utf-8") as f:
                f.write(text + "\n")
            print(f"{out}  {text!r} from {who}", flush=True)
            self._reply(200, "ok\n")
        elif path == "/photo":
            if len(data) < 4 or data[:2] != b"\xff\xd8":
                self._reply(400, "not a jpeg\n")
                return
            folder = os.path.join(self.inbox, "photos")
            os.makedirs(folder, exist_ok=True)
            out = unique_path(folder, stamp, ".jpg")
            with open(out, "wb") as f:
                f.write(data)
            print(f"{out}  ({len(data)} bytes from {who})", flush=True)
            self._reply(200, "ok\n")
        elif path == "/ink":
            if len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n":
                self._reply(400, "not a png\n")
                return
            folder = os.path.join(self.inbox, "ink")
            os.makedirs(folder, exist_ok=True)
            out = unique_path(folder, stamp, ".png")
            with open(out, "wb") as f:
                f.write(data)
            print(f"{out}  ({len(data)} bytes from {who})", flush=True)
            self._reply(200, "ok\n")
        else:
            self._reply(404, "unknown path (POST /hear, /msg, /photo, /ink)\n")

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
