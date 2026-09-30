#include "png.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static void *big_alloc(size_t n) { void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM); return p ? p : malloc(n); }
#else
static void *big_alloc(size_t n) { return malloc(n); }
#endif

/* ---- inflate (after Mark Adler's puff: canonical Huffman decoding, no tables, small and slow enough) ---------- */

#define MAXBITS 15
typedef struct { short count[MAXBITS + 1]; short symbol[288]; } huff_t;
typedef struct {
    const uint8_t *in; size_t inlen, incnt;
    uint32_t bitbuf; int bitcnt;
    uint8_t *out; size_t outlen, outcnt;
} state_t;

static int getbits(state_t *s, int need)
{
    long val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) return -1;
        val |= (long)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = (uint32_t)(val >> need);
    s->bitcnt -= need;
    return (int)(val & ((1L << need) - 1));
}

static int decode(state_t *s, const huff_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        int b = getbits(s, 1);
        if (b < 0) return -1;
        code |= b;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int construct(huff_t *h, const short *length, int n)
{
    short offs[MAXBITS + 1];
    for (int len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) offs[len + 1] = offs[len] + h->count[len];
    for (int sym = 0; sym < n; sym++)
        if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (short)sym;
    return left;
}

static int codes(state_t *s, const huff_t *lencode, const huff_t *distcode)
{
    static const short lens[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const short dists[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    int symbol;
    do {
        symbol = decode(s, lencode);
        if (symbol < 0) return -1;
        if (symbol < 256) {
            if (s->outcnt >= s->outlen) return -1;
            s->out[s->outcnt++] = (uint8_t)symbol;
        } else if (symbol > 256) {
            symbol -= 257;
            if (symbol >= 29) return -1;
            int e = getbits(s, lext[symbol]);
            if (e < 0) return -1;
            int len = lens[symbol] + e;
            symbol = decode(s, distcode);
            if (symbol < 0 || symbol >= 30) return -1;
            e = getbits(s, dext[symbol]);
            if (e < 0) return -1;
            size_t dist = (size_t)dists[symbol] + (size_t)e;
            if (dist > s->outcnt || s->outcnt + (size_t)len > s->outlen) return -1;
            while (len--) { s->out[s->outcnt] = s->out[s->outcnt - dist]; s->outcnt++; }
        }
    } while (symbol != 256);
    return 0;
}

static int stored_block(state_t *s)
{
    s->bitbuf = 0; s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return -1;
    unsigned len = s->in[s->incnt] | (s->in[s->incnt + 1] << 8);
    unsigned nlen = s->in[s->incnt + 2] | (s->in[s->incnt + 3] << 8);
    s->incnt += 4;
    if (len != (~nlen & 0xFFFF)) return -1;
    if (s->incnt + len > s->inlen || s->outcnt + len > s->outlen) return -1;
    memcpy(s->out + s->outcnt, s->in + s->incnt, len);
    s->incnt += len; s->outcnt += len;
    return 0;
}

static int fixed_block(state_t *s)
{
    huff_t lencode, distcode;
    short lengths[288];
    int sym = 0;
    for (; sym < 144; sym++) lengths[sym] = 8;
    for (; sym < 256; sym++) lengths[sym] = 9;
    for (; sym < 280; sym++) lengths[sym] = 7;
    for (; sym < 288; sym++) lengths[sym] = 8;
    construct(&lencode, lengths, 288);
    for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
    construct(&distcode, lengths, 30);
    return codes(s, &lencode, &distcode);
}

static int dynamic_block(state_t *s)
{
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[320];
    huff_t lencode, distcode;
    int nlen = getbits(s, 5), ndist = getbits(s, 5), ncode = getbits(s, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0) return -1;
    nlen += 257; ndist += 1; ncode += 4;
    if (nlen > 286 || ndist > 30) return -1;
    int index;
    for (index = 0; index < ncode; index++) {
        int v = getbits(s, 3);
        if (v < 0) return -1;
        lengths[order[index]] = (short)v;
    }
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (construct(&lencode, lengths, 19) != 0) return -1;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lencode);
        if (symbol < 0) return -1;
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
        } else {
            int len = 0, rep;
            if (symbol == 16) {
                if (index == 0) return -1;
                len = lengths[index - 1];
                rep = getbits(s, 2); if (rep < 0) return -1; rep += 3;
            } else if (symbol == 17) {
                rep = getbits(s, 3); if (rep < 0) return -1; rep += 3;
            } else {
                rep = getbits(s, 7); if (rep < 0) return -1; rep += 11;
            }
            if (index + rep > nlen + ndist) return -1;
            while (rep--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) return -1;
    int err = construct(&lencode, lengths, nlen);
    if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) return -1;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) return -1;
    return codes(s, &lencode, &distcode);
}

long png_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen)
{
    state_t s = { .in = in, .inlen = inlen, .out = out, .outlen = outlen };
    int last;
    do {
        last = getbits(&s, 1);
        int type = getbits(&s, 2);
        if (last < 0 || type < 0) return -1;
        int err = type == 0 ? stored_block(&s) : type == 1 ? fixed_block(&s) : type == 2 ? dynamic_block(&s) : -1;
        if (err) return -1;
    } while (!last);
    return (long)s.outcnt;
}

/* ---- PNG container ---------------------------------------------------------------------------------------------- */

typedef struct {
    int w, h, depth, ctype, ch;
    size_t rowbytes;
    uint8_t *raw;                 /* h rows of (1 + rowbytes): filter byte, then the unfiltered row */
    uint8_t pal[256][4];
    int npal;
} png_t;

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static bool header(const uint8_t *d, size_t n, png_t *p)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (n < 33 || memcmp(d, sig, 8) || memcmp(d + 12, "IHDR", 4)) return false;
    p->w = (int)be32(d + 16);
    p->h = (int)be32(d + 20);
    p->depth = d[24];
    p->ctype = d[25];
    if (p->w <= 0 || p->h <= 0 || p->w > PNG_MAX_DIM || p->h > PNG_MAX_DIM) return false;
    if (d[26] != 0 || d[27] != 0 || d[28] != 0) return false;               /* compression / filter method / interlace */
    switch (p->ctype) {
    case 0: p->ch = 1; if (p->depth != 1 && p->depth != 2 && p->depth != 4 && p->depth != 8 && p->depth != 16) return false; break;
    case 2: p->ch = 3; if (p->depth != 8 && p->depth != 16) return false; break;
    case 3: p->ch = 1; if (p->depth != 1 && p->depth != 2 && p->depth != 4 && p->depth != 8) return false; break;
    case 4: p->ch = 2; if (p->depth != 8 && p->depth != 16) return false; break;
    case 6: p->ch = 4; if (p->depth != 8 && p->depth != 16) return false; break;
    default: return false;
    }
    p->rowbytes = ((size_t)p->w * p->ch * p->depth + 7) / 8;
    return true;
}

bool png_info(const uint8_t *d, size_t n, int *w, int *h)
{
    png_t p;
    if (!header(d, n, &p)) return false;
    *w = p.w; *h = p.h;
    return true;
}

static int paeth(int a, int b, int c)
{
    int pp = a + b - c, pa = abs(pp - a), pb = abs(pp - b), pc = abs(pp - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

static void free_png(png_t *p) { free(p->raw); p->raw = NULL; }

static bool load(const uint8_t *d, size_t n, png_t *p)
{
    memset(p, 0, sizeof *p);
    if (!header(d, n, p)) return false;
    size_t rawlen = (p->rowbytes + 1) * (size_t)p->h;
    if (rawlen > PNG_MAX_RAW) return false;
    uint8_t *idat = big_alloc(n);
    p->raw = big_alloc(rawlen);
    if (!idat || !p->raw) { free(idat); free_png(p); return false; }
    for (int i = 0; i < 256; i++) p->pal[i][3] = 255;
    size_t idat_len = 0, pos = 8;
    bool end = false;
    while (pos + 12 <= n && !end) {
        uint32_t len = be32(d + pos);
        const uint8_t *type = d + pos + 4, *body = d + pos + 8;
        if (len > n - pos - 12) break;
        if (!memcmp(type, "IDAT", 4)) { memcpy(idat + idat_len, body, len); idat_len += len; }
        else if (!memcmp(type, "PLTE", 4)) {
            p->npal = (int)(len / 3) > 256 ? 256 : (int)(len / 3);
            for (int i = 0; i < p->npal; i++) { p->pal[i][0] = body[3 * i]; p->pal[i][1] = body[3 * i + 1]; p->pal[i][2] = body[3 * i + 2]; }
        } else if (!memcmp(type, "tRNS", 4) && p->ctype == 3) {
            for (uint32_t i = 0; i < len && i < 256; i++) p->pal[i][3] = body[i];
        } else if (!memcmp(type, "IEND", 4)) end = true;
        pos += 12 + len;
    }
    bool ok = idat_len > 6 && (idat[0] & 0x0F) == 8;                   /* zlib, deflate */
    if (ok) ok = png_inflate(idat + 2, idat_len - 2, p->raw, rawlen) == (long)rawlen;
    free(idat);
    if (!ok) { free_png(p); return false; }
    /* undo the row filters in place */
    const size_t bpp = p->depth >= 8 ? (size_t)p->ch * (size_t)p->depth / 8 : 1;
    const size_t rb = p->rowbytes;
    for (int y = 0; y < p->h; y++) {
        uint8_t *row = p->raw + (size_t)y * (rb + 1);
        uint8_t ft = row[0];
        uint8_t *cur = row + 1;
        const uint8_t *prev = y ? row - (rb + 1) + 1 : NULL;
        for (size_t x = 0; x < rb; x++) {
            int a = x >= bpp ? cur[x - bpp] : 0, b = prev ? prev[x] : 0, c = (prev && x >= bpp) ? prev[x - bpp] : 0;
            switch (ft) {
            case 0: break;
            case 1: cur[x] = (uint8_t)(cur[x] + a); break;
            case 2: cur[x] = (uint8_t)(cur[x] + b); break;
            case 3: cur[x] = (uint8_t)(cur[x] + ((a + b) >> 1)); break;
            case 4: cur[x] = (uint8_t)(cur[x] + paeth(a, b, c)); break;
            default: free_png(p); return false;
            }
        }
    }
    return true;
}

static void pixel(const png_t *p, int x, int y, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    const uint8_t *row = p->raw + (size_t)y * (p->rowbytes + 1) + 1;
    *a = 255;
    if (p->depth < 8) {                                   /* gray or palette, packed */
        int per = 8 / p->depth, shift = 8 - p->depth * (x % per + 1), mask = (1 << p->depth) - 1;
        int v = (row[x / per] >> shift) & mask;
        if (p->ctype == 3) {
            *r = p->pal[v][0]; *g = p->pal[v][1]; *b = p->pal[v][2]; *a = p->pal[v][3];
        } else {
            *r = *g = *b = (uint8_t)(v * 255 / mask);
        }
        return;
    }
    const int step = p->depth / 8;                        /* bytes per sample: 1 or 2 (take the high byte) */
    const uint8_t *px = row + (size_t)x * p->ch * step;
    switch (p->ctype) {
    case 0: *r = *g = *b = px[0]; break;
    case 2: *r = px[0]; *g = px[step]; *b = px[2 * step]; break;
    case 3: *r = p->pal[px[0]][0]; *g = p->pal[px[0]][1]; *b = p->pal[px[0]][2]; *a = p->pal[px[0]][3]; break;
    case 4: *r = *g = *b = px[0]; *a = px[step]; break;
    default: *r = px[0]; *g = px[step]; *b = px[2 * step]; *a = px[3 * step]; break;
    }
}

static void fit(int w, int h, int max_w, int max_h, int *ow, int *oh)
{
    *ow = w; *oh = h;
    if (w > max_w || h > max_h) {
        if ((long)max_w * h <= (long)max_h * w) { *ow = max_w; *oh = (int)((long)h * max_w / w); }
        else { *oh = max_h; *ow = (int)((long)w * max_h / h); }
        if (*ow < 1) *ow = 1;
        if (*oh < 1) *oh = 1;
    }
}

bool png_decode_rgb565(const uint8_t *d, size_t n, int max_w, int max_h, uint16_t **out, int *ow, int *oh)
{
    png_t p;
    if (max_w < 1 || max_h < 1 || !load(d, n, &p)) return false;
    fit(p.w, p.h, max_w, max_h, ow, oh);
    uint16_t *o = big_alloc((size_t)*ow * *oh * 2);
    if (!o) { free_png(&p); return false; }
    for (int y = 0; y < *oh; y++) {
        int sy0 = (int)((long)y * p.h / *oh), sy1 = (int)((long)(y + 1) * p.h / *oh);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < *ow; x++) {
            int sx0 = (int)((long)x * p.w / *ow), sx1 = (int)((long)(x + 1) * p.w / *ow);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            unsigned sr = 0, sg = 0, sb = 0, cnt = 0;
            for (int yy = sy0; yy < sy1; yy++)
                for (int xx = sx0; xx < sx1; xx++) {
                    uint8_t r, g, b, a;
                    pixel(&p, xx, yy, &r, &g, &b, &a);
                    sr += (unsigned)r * a / 255; sg += (unsigned)g * a / 255; sb += (unsigned)b * a / 255; cnt++;
                }
            unsigned r = sr / cnt, g = sg / cnt, b = sb / cnt;
            uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            o[(size_t)y * *ow + x] = (uint16_t)((v << 8) | (v >> 8));
        }
    }
    free_png(&p);
    *out = o;
    return true;
}

bool png_decode_ink_mask(const uint8_t *d, size_t n, int max_w, int max_h, uint8_t **out, int *ow, int *oh)
{
    png_t p;
    if (max_w < 1 || max_h < 1 || !load(d, n, &p)) return false;
    /* pass 1: is there transparency? what is the background? how strong is the ink? */
    int min_a = 255;
    int bg = 0;
    const int cx[4] = { 0, p.w - 1, 0, p.w - 1 }, cy[4] = { 0, 0, p.h - 1, p.h - 1 };
    for (int i = 0; i < 4; i++) {
        uint8_t r, g, b, a;
        pixel(&p, cx[i], cy[i], &r, &g, &b, &a);
        bg += (r * 30 + g * 59 + b * 11) / 100;
    }
    bg /= 4;
    int maxdiff = 0;
    for (int y = 0; y < p.h; y++)
        for (int x = 0; x < p.w; x++) {
            uint8_t r, g, b, a;
            pixel(&p, x, y, &r, &g, &b, &a);
            if (a < min_a) min_a = a;
            int diff = abs((r * 30 + g * 59 + b * 11) / 100 - bg);
            if (diff > maxdiff) maxdiff = diff;
        }
    const bool by_alpha = min_a < 128;
    fit(p.w, p.h, max_w, max_h, ow, oh);
    uint8_t *o = big_alloc((size_t)*ow * *oh);
    if (!o) { free_png(&p); return false; }
    for (int y = 0; y < *oh; y++) {
        int sy0 = (int)((long)y * p.h / *oh), sy1 = (int)((long)(y + 1) * p.h / *oh);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < *ow; x++) {
            int sx0 = (int)((long)x * p.w / *ow), sx1 = (int)((long)(x + 1) * p.w / *ow);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            unsigned sum = 0, cnt = 0;
            for (int yy = sy0; yy < sy1; yy++)
                for (int xx = sx0; xx < sx1; xx++) {
                    uint8_t r, g, b, a;
                    pixel(&p, xx, yy, &r, &g, &b, &a);
                    unsigned cov;
                    if (by_alpha) cov = a;
                    else if (maxdiff < 16) cov = 0;
                    else {
                        unsigned diff = (unsigned)abs((r * 30 + g * 59 + b * 11) / 100 - bg);
                        cov = diff * 255 / (unsigned)maxdiff;
                        if (cov > 255) cov = 255;
                    }
                    sum += cov; cnt++;
                }
            o[(size_t)y * *ow + x] = (uint8_t)(sum / cnt);
        }
    }
    free_png(&p);
    *out = o;
    return true;
}
