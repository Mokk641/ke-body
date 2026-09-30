#!/usr/bin/env python3
"""
Generate 4-bit anti-aliased bitmap fonts as C source for the ke-body firmware.

Usage:  python3 tools/gen_fonts.py            (writes main/fonts/*.c and main/fonts/fonts.h)

Requires Pillow + fontTools (see README). Source fonts (all present on a normal
Debian/Ubuntu box):
  - WenQuanYi Zen Hei  (/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc)   CJK + symbols
  - DejaVu Sans        (/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf) Latin + symbols
  - GNU Unifont        (/usr/share/fonts/opentype/unifont/unifont.otf)   last-resort fallback

Glyph record layout must match kb_glyph_t / kb_font_t in main/kb_font.h.
Bitmap format: 4 bpp alpha, high nibble = left pixel, rows padded to whole bytes.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont, TTCollection

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "..", "main", "fonts")

WQY = "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"
DEJAVU = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
UNIFONT = "/usr/share/fonts/opentype/unifont/unifont.otf"

# --- character sets ---------------------------------------------------------

ASCII = "".join(chr(c) for c in range(0x20, 0x7F))

# Symbols commonly used in kaomoji / 颜文字.  Missing glyphs are skipped
# (the renderer draws a hollow box for anything not in the font).
KAOMOJI = (
    "—_︵♡∀ω´`・°˘ᵕ≧≦○●◕◡╥¬•˙✧☆★♪～￣□Д≡ˊˋ益ヮノТ▽∇ˇ▔＿｀＾ｏ〃Ω゜ε｡ᴗ⌒꒳…"
    "（）＜＞／＼｜︶︿ˆ˚·＾￣ᗜ▿△ᴥ◠◉◔ㅅ๑´∀｀ﾉ≧▽≦ˇ▽ˇ￣▽￣＞＜ㅁ"
    "ﾟ∩∪◇◆♥♪♬✿❀☆★☀☁☂☃♨☺☻✓✔✗✘⊙⊙︿︶〜"
    "ヽヾ〇ﾉ゛゜ヘへ∠☜☞ღ∑屮凸┌┐└┘─│┬┴┻━╯╰╭╮︹︺﹀ﾞ･✩♀♂✌☝〒ミ彡"
    "αβγδθλμπστφψ∞≈≠≤≥±√∴∵♩♫〆ヾ〃ᵒᵔᵕᵘᵛꈍ‿◞◟ᐛᐖ"
    "、。，！？：；“”‘’「」『』《》【】"
    "ˍ皿﹏"
)

CJK_PUNCT = "，。、！？：；“”‘’（）《》〈〉【】『』「」…—～·￥％＋－＝×÷℃"
FULLWIDTH_DIGITS = "".join(chr(c) for c in range(0xFF10, 0xFF1A))


def gb2312_hanzi():
    """All 6763 GB2312 hanzi (rows 16-87): level 1 (3755, most common) + level 2 (3008).
    This is a superset of the 3500 常用字 and also covers chat words like 嗯/呗/咋."""
    out = []
    for row in range(0xB0, 0xF8):
        for col in range(0xA1, 0xFF):
            try:
                out.append(bytes([row, col]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return "".join(out)


STATUS_CJK = ("等待配网连接中无网络已断开未错误重试在听发送失败播放录音"
              "相机册删除寄给克返回拍照存请插卡张让看没有片到内部成功正睡觉摇一下想你了抱在干嘛晚安眼睛远程模式")  # corner status words

def source_cjk():
    """Every CJK ideograph that appears in the firmware sources (main/*.c, *.h, *.cpp),
    so any label drawn with the small font always has its glyph."""
    chars = set()
    src = os.path.join(HERE, "..", "main")
    for fn in sorted(os.listdir(src)):
        if fn.endswith((".c", ".h", ".cpp")):
            with open(os.path.join(src, fn), encoding="utf-8") as f:
                for ch in f.read():
                    if "\u4e00" <= ch <= "\u9fff":
                        chars.add(ch)
    return "".join(sorted(chars))


# --- helpers ----------------------------------------------------------------

_cmap_cache = {}


def cmap_of(path):
    if path not in _cmap_cache:
        if path.endswith(".ttc"):
            f = TTCollection(path).fonts[0]
        else:
            f = TTFont(path)
        _cmap_cache[path] = f.getBestCmap()
    return _cmap_cache[path]


def pick_font(cp, chain):
    for path in chain:
        if cp in cmap_of(path):
            return path
    return None


def render_glyph(font, ch):
    """Return (bitmap4bpp bytes, w, h, xoff, yoff, advance) for one char."""
    size = font.size
    pad = size
    W = size * 3 + pad * 2
    H = size * 3 + pad * 2
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    ox, oy = pad, pad + size * 2  # pen position (baseline-left)
    d.text((ox, oy), ch, font=font, fill=255, anchor="ls")
    adv = int(round(font.getlength(ch)))
    bbox = img.getbbox()
    if bbox is None:
        return b"", 0, 0, 0, 0, adv
    x0, y0, x1, y1 = bbox
    w, h = x1 - x0, y1 - y0
    px = img.load()
    stride = (w + 1) // 2
    data = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            v = px[x0 + x, y0 + y] >> 4
            i = y * stride + x // 2
            if x & 1:
                data[i] |= v
            else:
                data[i] |= v << 4
    return bytes(data), w, h, x0 - ox, y0 - oy, adv


# --- vector icons for characters that only exist as colour emoji -------------
# Drawn with Pillow as white line art, same height as the font's ascent, so the
# renderer treats them like any other glyph.

ICON_CODEPOINTS = {0x1F4A2: "anger", 0x1F4A7: "droplet"}   # 💢 💧


def render_icon(kind, size, ascent):
    """Return (bitmap4bpp, w, h, xoff, yoff, adv). Rendered 4x and downsampled for AA."""
    S = int(ascent * 0.95)          # icon box side in pixels
    if S < 8:
        S = 8
    K = 4
    B = S * K
    img = Image.new("L", (B, B), 0)
    d = ImageDraw.Draw(img)
    lw = max(1, int(B * 0.11))      # line width
    if kind == "anger":
        # 💢: four arcs bowing toward the centre (comic anger mark)
        r = int(B * 0.58)
        corners = [(0, 0), (B, 0), (0, B), (B, B)]
        starts = [(0, 90), (90, 180), (270, 360), (180, 270)]
        for (cx, cy), (a0, a1) in zip(corners, starts):
            box = (cx - r, cy - r, cx + r, cy + r)
            d.arc(box, a0 + 8, a1 - 8, fill=255, width=lw)
        # trim to the glyph box: arcs already clipped by the image bounds
    elif kind == "droplet":
        # 💧: teardrop outline, pointed top, round bottom
        cx = B // 2
        rr = int(B * 0.30)
        cy = int(B * 0.66)
        top = int(B * 0.04)
        import math
        # tangent points from the top apex to the circle
        dx, dy = 0, cy - top
        dist = math.hypot(dx, dy)
        ang = math.asin(rr / dist)
        base = math.atan2(dy, dx)
        pts = []
        for s in (-1, 1):
            t = base + s * ang
            # tangent point on the circle
            px = cx + rr * math.cos(t - s * math.pi / 2)
            py = cy + rr * math.sin(t - s * math.pi / 2)
            pts.append((px, py))
        def shape(shrink, fill):
            d.ellipse((cx - rr + shrink, cy - rr + shrink, cx + rr - shrink, cy + rr - shrink), fill=fill)
            # triangle apex .. tangent points (shrunk toward the centroid)
            tri = [(cx, top + shrink * 1.6), pts[0], pts[1]]
            gx = sum(p[0] for p in tri) / 3
            gy = sum(p[1] for p in tri) / 3
            tri2 = [(gx + (x - gx) * (1 - shrink * 1.2 / max(1, rr)), gy + (y - gy) * (1 - shrink * 1.2 / max(1, rr))) for x, y in tri]
            d.polygon(tri2, fill=fill)
        shape(0, 255)
        shape(lw, 0)
    small = img.resize((S, S), Image.LANCZOS)
    bbox = small.getbbox()
    if bbox is None:
        return b"", 0, 0, 0, 0, S
    x0, y0, x1, y1 = bbox
    w, h = x1 - x0, y1 - y0
    px = small.load()
    stride = (w + 1) // 2
    data = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            v = px[x0 + x, y0 + y] >> 4
            i = y * stride + x // 2
            data[i] |= (v << 4) if (x % 2 == 0) else v
    pad = max(1, S // 10)
    # sit on the baseline, top aligned with the cap height
    yoff = -(ascent - (ascent - S) // 2) + y0
    return bytes(data), w, h, pad + x0, yoff, w + 2 * pad + 0


def build_font(name, size, chars, chain):
    """chain: list of font paths in priority order."""
    fonts = {}
    glyphs = []
    blob = bytearray()
    seen = set()
    missing = []
    for ch in chars:
        cp = ord(ch)
        if cp in seen:
            continue
        seen.add(cp)
        path = pick_font(cp, chain)
        if path is None:
            missing.append(ch)
            continue
        if path not in fonts:
            fonts[path] = ImageFont.truetype(path, size)
        data, w, h, xoff, yoff, adv = render_glyph(fonts[path], ch)
        glyphs.append((cp, w, h, xoff, yoff, adv, len(blob)))
        blob += data
    primary = ImageFont.truetype(chain[0], size)
    ascent, descent = primary.getmetrics()
    for cp, kind in ICON_CODEPOINTS.items():
        if cp in seen:
            continue
        data, w, h, xoff, yoff, adv = render_icon(kind, size, ascent)
        glyphs.append((cp, w, h, xoff, yoff, adv, len(blob)))
        blob += data
        seen.add(cp)
    glyphs.sort()
    line_h = ascent + descent
    if missing:
        print(f"  [{name}] {len(missing)} chars have no glyph in any source font: {''.join(missing)}")
    print(f"  [{name}] size={size} glyphs={len(glyphs)} bitmap={len(blob)} bytes")
    return dict(name=name, size=size, glyphs=glyphs, blob=bytes(blob),
                ascent=ascent, descent=descent, line_h=line_h)


def emit_c(f):
    name = f["name"]
    path = os.path.join(OUT_DIR, f"font_{name}.c")
    with open(path, "w", encoding="utf-8") as o:
        o.write("/* Generated by tools/gen_fonts.py - do not edit. */\n")
        o.write('#include "kb_font.h"\n\n')
        o.write(f"static const uint8_t {name}_bitmap[] = {{\n")
        blob = f["blob"]
        for i in range(0, len(blob), 24):
            o.write("  " + ",".join(f"0x{b:02x}" for b in blob[i:i + 24]) + ",\n")
        if not blob:
            o.write("  0\n")
        o.write("};\n\n")
        o.write(f"static const kb_glyph_t {name}_glyphs[] = {{\n")
        for cp, w, h, xoff, yoff, adv, off in f["glyphs"]:
            o.write(f"  {{0x{cp:05x},{w},{h},{xoff},{yoff},{adv},{off}}},\n")
        o.write("};\n\n")
        o.write(f"const kb_font_t kb_font_{name} = {{\n")
        o.write(f"  .glyphs = {name}_glyphs,\n  .count = {len(f['glyphs'])},\n")
        o.write(f"  .bitmap = {name}_bitmap,\n  .ascent = {f['ascent']},\n")
        o.write(f"  .descent = {f['descent']},\n  .line_height = {f['line_h']},\n  .size = {f['size']},\n}};\n")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for p in (WQY, DEJAVU, UNIFONT):
        if not os.path.exists(p):
            sys.exit(f"missing source font: {p}")

    face_chars = ASCII + KAOMOJI
    latin_first = [DEJAVU, WQY, UNIFONT]
    cjk_first = [WQY, DEJAVU, UNIFONT]

    fonts = [
        build_font("face96", 96, face_chars, latin_first),   # big face on the face page
        build_font("face64", 64, face_chars, latin_first),
        build_font("face44", 44, face_chars, latin_first),
        build_font("face30", 30, face_chars, latin_first),
        build_font("face18", 18, face_chars, latin_first),   # chat avatar / top bar / emoji grid
        build_font("face13", 13, face_chars, latin_first),
        build_font("text22", 22, ASCII + CJK_PUNCT + FULLWIDTH_DIGITS + KAOMOJI + gb2312_hanzi(), cjk_first),
        build_font("small14", 14, ASCII + STATUS_CJK + source_cjk() + CJK_PUNCT, cjk_first),
    ]
    for f in fonts:
        emit_c(f)
    with open(os.path.join(OUT_DIR, "fonts.h"), "w") as o:
        o.write("/* Generated by tools/gen_fonts.py - do not edit. */\n#pragma once\n#include \"kb_font.h\"\n\n")
        for f in fonts:
            o.write(f"extern const kb_font_t kb_font_{f['name']};\n")
    total = sum(len(f["blob"]) for f in fonts)
    print(f"total bitmap bytes: {total}")


if __name__ == "__main__":
    main()
