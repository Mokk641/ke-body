#!/usr/bin/env python3
"""
ke_send.py - send things to the ke-body board.

    ke_send.py --board 192.168.1.23 face "(—ω—)"
    ke_send.py say "你好呀"
    ke_send.py play hello.wav          # PCM 16-bit mono, 16000 or 24000 Hz
    ke_send.py volume 60
    ke_send.py ping

The board address comes from --board or the KE_BOARD environment variable
(IP or host[:port]). Standard library only.
"""
import argparse
import os
import struct
import sys
import urllib.error
import urllib.request


def wav_check(data):
    """Return a warning string for WAVs the board will reject or resample, else None."""
    if len(data) < 44 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return "not a RIFF/WAVE file"
    pos = 12
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        if cid == b"fmt ":
            fmt, ch, rate, _, _, bits = struct.unpack("<HHIIHH", data[pos + 8:pos + 24])
            problems = []
            if fmt not in (1, 0xFFFE):
                problems.append(f"format {fmt} is not PCM")
            if bits != 16:
                problems.append(f"{bits}-bit (need 16)")
            if ch not in (1, 2):
                problems.append(f"{ch} channels (need 1 or 2)")
            if rate not in (16000, 24000):
                problems.append(f"{rate} Hz (16000/24000 expected; board accepts 8k-48k)")
            return "; ".join(problems) or None
        pos += 8 + size + (size & 1)
    return "no fmt chunk"


def post(board, path, body, ctype):
    url = f"http://{board}{path}"
    req = urllib.request.Request(url, data=body, method="POST", headers={"Content-Type": ctype})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, r.read().decode("utf-8", "replace").strip()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace").strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--board", default=os.environ.get("KE_BOARD"), help="board IP (or env KE_BOARD)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("ping")
    sub.add_parser("face").add_argument("text")
    sub.add_parser("say").add_argument("text")
    sub.add_parser("play").add_argument("file")
    sub.add_parser("volume").add_argument("level", type=int)
    args = ap.parse_args()

    if not args.board:
        sys.exit("board address missing: use --board <ip> or set KE_BOARD")

    try:
        if args.cmd == "ping":
            with urllib.request.urlopen(f"http://{args.board}/ping", timeout=5) as r:
                code, text = r.status, r.read().decode().strip()
        elif args.cmd == "face":
            code, text = post(args.board, "/face", args.text.encode("utf-8"), "text/plain; charset=utf-8")
        elif args.cmd == "say":
            code, text = post(args.board, "/say", args.text.encode("utf-8"), "text/plain; charset=utf-8")
        elif args.cmd == "volume":
            code, text = post(args.board, "/volume", str(args.level).encode(), "text/plain")
        elif args.cmd == "play":
            with open(args.file, "rb") as f:
                data = f.read()
            warn = wav_check(data)
            if warn:
                print(f"warning: {args.file}: {warn}", file=sys.stderr)
            code, text = post(args.board, "/play", data, "audio/wav")
        else:
            ap.error("unknown command")
    except (urllib.error.URLError, OSError) as e:
        sys.exit(f"cannot reach board {args.board}: {e}")

    print(f"{code} {text}")
    return 0 if code == 200 else 1


if __name__ == "__main__":
    sys.exit(main())
