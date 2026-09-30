#include "lineedit.h"
#include <string.h>

static void out(lineedit_t *le, const char *s) { le->write(s, strlen(s)); }

/* Redraw the current line: works for any mix of narrow and wide characters
 * because the terminal does the layout. */
static void redraw(lineedit_t *le, const char *buf)
{
    out(le, "\r\x1b[K");
    out(le, le->prompt);
    out(le, buf);
}

static const char *hist_get(const lineedit_t *le, int back)   /* 1 = most recent */
{
    if (back < 1 || back > le->hist_n) return NULL;
    return le->hist[(le->hist_next + LINEEDIT_HIST_N - back) % LINEEDIT_HIST_N];
}

static void hist_add(lineedit_t *le, const char *line)
{
    if (!line[0]) return;
    const char *last = hist_get(le, 1);
    if (last && strcmp(last, line) == 0) return;
    strncpy(le->hist[le->hist_next], line, LINEEDIT_MAX - 1);
    le->hist[le->hist_next][LINEEDIT_MAX - 1] = 0;
    le->hist_next = (le->hist_next + 1) % LINEEDIT_HIST_N;
    if (le->hist_n < LINEEDIT_HIST_N) le->hist_n++;
}

int lineedit_read(lineedit_t *le, char *buf, size_t size)
{
    size_t cap = size < LINEEDIT_MAX ? size : LINEEDIT_MAX;
    size_t len = 0;
    int hist_pos = 0;                 /* 0 = the line being typed */
    char saved[LINEEDIT_MAX];
    saved[0] = 0;
    buf[0] = 0;
    out(le, le->prompt);

    for (;;) {
        int c = le->read();
        if (c < 0) return -1;
        int prev = le->prev;
        le->prev = c;

        if (c == '\n' && prev == '\r') continue;          /* second half of CR LF */

        if (c == '\r' || c == '\n') {
            out(le, "\r\n");
            buf[len] = 0;
            hist_add(le, buf);
            return (int)len;
        }
        if (c == 0x03) {                                   /* Ctrl-C */
            out(le, "^C\r\n");
            buf[0] = 0;
            return 0;
        }
        if (c == 0x15) {                                   /* Ctrl-U: clear the line */
            len = 0;
            buf[0] = 0;
            redraw(le, buf);
            continue;
        }
        if (c == 0x7f || c == 0x08) {                      /* backspace: one whole UTF-8 character */
            if (len == 0) continue;
            do {
                len--;
            } while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80);
            buf[len] = 0;
            redraw(le, buf);
            continue;
        }
        if (c == 0x1b) {                                   /* escape sequence: arrows for history, the rest ignored */
            int c1 = le->read();
            if (c1 != '[' && c1 != 'O') continue;
            int c2 = le->read();
            while (c2 >= 0x20 && c2 <= 0x3f) c2 = le->read();   /* parameter bytes, e.g. "3" in ESC [ 3 ~ */
            if (c2 == 'A') {                               /* up */
                const char *h = hist_get(le, hist_pos + 1);
                if (h) {
                    if (hist_pos == 0) { memcpy(saved, buf, len); saved[len] = 0; }
                    hist_pos++;
                    len = strlen(h);
                    if (len >= cap) len = cap - 1;
                    memcpy(buf, h, len);
                    buf[len] = 0;
                    redraw(le, buf);
                }
            } else if (c2 == 'B') {                        /* down */
                if (hist_pos > 0) {
                    hist_pos--;
                    const char *h = hist_pos ? hist_get(le, hist_pos) : saved;
                    len = strlen(h);
                    if (len >= cap) len = cap - 1;
                    memcpy(buf, h, len);
                    buf[len] = 0;
                    redraw(le, buf);
                }
            }
            continue;
        }
        if (c < 0x20) continue;                            /* other control characters (TAB, ...) */

        if (len + 1 < cap) {                               /* printable ASCII or any byte of a UTF-8 sequence */
            buf[len++] = (char)c;
            buf[len] = 0;
            char ch = (char)c;
            le->write(&ch, 1);
        }
    }
}
