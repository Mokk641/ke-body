#!/usr/bin/env python3
"""
ke_send.py - send things to the ke-body board.

    ke_send.py --board 192.168.1.23 face "(—ω—)"
    ke_send.py say "你好呀"
    ke_send.py play hello.wav          # PCM 16-bit mono, 16000 or 24000 Hz
    ke_send.py volume 60
    ke_send.py rotate 90               # 0 / 90 / 180 / 270
    ke_send.py brightness 40           # 5-100
    ke_send.py theme auto              # light / dark / auto (dark at night)
    ke_send.py heard "她说的话"         # speech-to-text result -> her side of the chat
    ke_send.py buttons buttons.json    # quick-button config (file or JSON string)
    ke_send.py anim off                # all animations on/off
    ke_send.py anim blink on           # one of: blink blush zzz shake flash
    ke_send.py anim status
    ke_send.py chime on                # soft tone on new message on/off
    ke_send.py peek on                 # allow remote snapshots (eye icon on the screen)
    ke_send.py snap photo.jpg          # take a photo (needs peek on) and save it here
    ke_send.py ink his_note.png        # Ke's handwriting (PNG): shows on his side of the chat, white ink
    ke_send.py image photo.jpg         # a picture (JPEG/PNG), shrunk to the screen size first; tap it on the board to see it full screen
    ke_send.py music song.mp3 [title]  # push a song: it starts playing at once and appears at the top of the list
    ke_send.py ping

The board address comes from --board or the KE_BOARD environment variable
(IP or host[:port]). HTTP proxy environment variables are ignored for the
board (it is on the LAN). Standard library only.
"""
import argparse
import os
import struct
import sys
import urllib.error
import urllib.request

# The board is on the LAN: never go through HTTP(S)_PROXY for it.
_opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


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


def image_size(data):
    """(width, height) of a PNG or JPEG, or None."""
    if data[:8] == b"\x89PNG\r\n\x1a\n" and len(data) > 24:
        return struct.unpack(">II", data[16:24])
    if data[:2] == b"\xff\xd8":
        i = 2
        while i + 9 < len(data):
            if data[i] != 0xFF:
                i += 1
                continue
            m = data[i + 1]
            if m in (0xD8, 0x01) or 0xD0 <= m <= 0xD7:
                i += 2
                continue
            seg = struct.unpack(">H", data[i + 2:i + 4])[0]
            if m in (0xC0, 0xC1, 0xC2):
                h, w = struct.unpack(">HH", data[i + 5:i + 9])
                return w, h
            i += 2 + seg
    return None


def prepare_image(path, max_w=480, max_h=480):
    """(bytes, content-type) ready for POST /image: shrunk to the screen size and turned into a JPEG when Pillow is
    installed; without Pillow only a picture that is small enough already can be sent as it is."""
    with open(path, "rb") as f:
        data = f.read()
    try:
        from PIL import Image, ImageOps
    except ImportError:
        Image = None
    if Image is not None:
        import io
        img = ImageOps.exif_transpose(Image.open(io.BytesIO(data)))
        if img.mode in ("RGBA", "LA", "P"):
            img = img.convert("RGBA")
            bg = Image.new("RGB", img.size, (0, 0, 0))
            bg.paste(img, mask=img.split()[-1])
            img = bg
        else:
            img = img.convert("RGB")
        img.thumbnail((max_w, max_h))
        out = io.BytesIO()
        img.save(out, "JPEG", quality=88)
        return out.getvalue(), "image/jpeg"
    size = image_size(data)
    if size is None:
        sys.exit("not a JPEG or PNG file")
    if size[0] > max_w or size[1] > max_h or len(data) > 600 * 1024:
        sys.exit(f"{path} is {size[0]}x{size[1]} ({len(data) // 1024} KB): install Pillow (pip install pillow) so it can be shrunk to the screen size first")
    return data, ("image/png" if data[:4] == b"\x89PNG" else "image/jpeg")


def request(board, path, body=None, ctype="text/plain; charset=utf-8", timeout=20, headers=None):
    url = f"http://{board}{path}"
    if body is None:
        req = urllib.request.Request(url, method="GET")
    else:
        h = {"Content-Type": ctype}
        h.update(headers or {})
        req = urllib.request.Request(url, data=body, method="POST", headers=h)
    try:
        with _opener.open(req, timeout=timeout) as r:
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
    sub.add_parser("rotate").add_argument("degrees", type=int, choices=[0, 90, 180, 270])
    sub.add_parser("brightness").add_argument("level", type=int)
    sub.add_parser("theme").add_argument("name", choices=["light", "dark", "auto"])
    sub.add_parser("ink").add_argument("file", help="PNG of handwriting (any colours; the board shows it as white ink)")
    sub.add_parser("image").add_argument("file", help="JPEG or PNG")
    sp = sub.add_parser("music")
    sp.add_argument("file", help="an MP3 file")
    sp.add_argument("title", nargs="?", help="name shown on the board (default: the file name)")
    sub.add_parser("heard").add_argument("text")
    sub.add_parser("buttons").add_argument("json", help="a .json file, a JSON string, or the word reset")
    sp = sub.add_parser("anim")
    sp.add_argument("args", nargs="+", metavar="[name] on|off|status",
                    help="on|off (all)  |  blink|blush|zzz|shake|flash on|off  |  status")
    sub.add_parser("chime").add_argument("state", choices=["on", "off"])
    sub.add_parser("peek").add_argument("state", choices=["on", "off"])
    sub.add_parser("snap").add_argument("out", nargs="?", default="snap.jpg")
    args = ap.parse_args()

    if not args.board:
        sys.exit("board address missing: use --board <ip> or set KE_BOARD")

    try:
        if args.cmd == "ping":
            code, text = request(args.board, "/ping", timeout=5)
        elif args.cmd == "face":
            code, text = request(args.board, "/face", args.text.encode("utf-8"))
        elif args.cmd == "say":
            code, text = request(args.board, "/say", args.text.encode("utf-8"))
        elif args.cmd == "volume":
            code, text = request(args.board, "/volume", str(args.level).encode())
        elif args.cmd == "rotate":
            code, text = request(args.board, "/rotate", str(args.degrees).encode())
        elif args.cmd == "brightness":
            code, text = request(args.board, "/brightness", str(args.level).encode())
        elif args.cmd == "theme":
            code, text = request(args.board, "/theme", args.name.encode())
        elif args.cmd == "ink":
            with open(args.file, "rb") as f:
                data = f.read()
            code, text = request(args.board, "/ink", data, "image/png", timeout=30)
        elif args.cmd == "image":
            data, ctype = prepare_image(args.file)
            code, text = request(args.board, "/image", data, ctype, timeout=60)
        elif args.cmd == "music":
            import urllib.parse
            with open(args.file, "rb") as f:
                data = f.read()
            title = args.title or os.path.splitext(os.path.basename(args.file))[0]
            print(f"sending {len(data) // 1024} KB ...", flush=True)
            code, text = request(args.board, "/music", data, "audio/mpeg", timeout=600,
                                 headers={"X-Title": urllib.parse.quote(title)})
        elif args.cmd == "heard":
            code, text = request(args.board, "/heard", args.text.encode("utf-8"))
        elif args.cmd == "buttons":
            body = args.json
            if body != "reset" and os.path.exists(body):
                with open(body, "r", encoding="utf-8") as f:
                    body = f.read()
            code, text = request(args.board, "/buttons", body.encode("utf-8"), "application/json")
        elif args.cmd == "anim":
            code, text = request(args.board, "/anim", " ".join(args.args[:2]).encode())
        elif args.cmd in ("chime", "peek"):
            code, text = request(args.board, "/" + args.cmd, args.state.encode())
        elif args.cmd == "snap":
            req = urllib.request.Request(f"http://{args.board}/snap", method="GET")
            try:
                with _opener.open(req, timeout=30) as r:
                    data = r.read()
                    code = r.status
                if code == 200 and data[:2] == b"\xff\xd8":
                    with open(args.out, "wb") as f:
                        f.write(data)
                    text = f"saved {args.out} ({len(data)} bytes)"
                else:
                    text = data.decode("utf-8", "replace").strip()
            except urllib.error.HTTPError as e:
                code, text = e.code, e.read().decode("utf-8", "replace").strip()
        elif args.cmd == "play":
            with open(args.file, "rb") as f:
                data = f.read()
            warn = wav_check(data)
            if warn:
                print(f"warning: {args.file}: {warn}", file=sys.stderr)
            code, text = request(args.board, "/play", data, "audio/wav")
        else:
            ap.error("unknown command")
    except (urllib.error.URLError, OSError) as e:
        sys.exit(f"cannot reach board {args.board}: {e}")

    print(f"{code} {text}")
    return 0 if code == 200 else 1


if __name__ == "__main__":
    sys.exit(main())
