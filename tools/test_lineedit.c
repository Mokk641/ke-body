/* Host test for main/lineedit.c (the UTF-8 aware console line editor).
 *
 *   gcc -Wall -Wextra -std=c99 -Imain -o /tmp/test_lineedit tools/test_lineedit.c main/lineedit.c && /tmp/test_lineedit
 */
#include <stdio.h>
#include <string.h>
#include "lineedit.h"

static const char *s_in;
static size_t s_pos;
static char s_out[4096];
static size_t s_out_n;

static int rd(void) { return s_pos < strlen(s_in) ? (unsigned char)s_in[s_pos++] : -1; }
static void wr(const char *s, size_t n)
{
    if (s_out_n + n < sizeof s_out) { memcpy(s_out + s_out_n, s, n); s_out_n += n; s_out[s_out_n] = 0; }
}

static int fails;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } else printf("ok:   %s\n", msg); } while (0)

static int run(lineedit_t *le, const char *input, char *line)
{
    s_in = input; s_pos = 0; s_out_n = 0; s_out[0] = 0;
    return lineedit_read(le, line, LINEEDIT_MAX);
}

int main(void)
{
    static lineedit_t le;
    le.read = rd; le.write = wr; le.prompt = "ke-body> ";
    char line[LINEEDIT_MAX];
    int n;

    n = run(&le, "msg 想你了\n", line);
    CHECK(n == (int)strlen("msg 想你了") && strcmp(line, "msg 想你了") == 0, "Chinese passes through byte-exact");

    n = run(&le, "msg 💧♡ok\r", line);
    CHECK(strcmp(line, "msg 💧♡ok") == 0, "4-byte emoji and symbols pass through");

    n = run(&le, "ab想\x7f\n", line);
    CHECK(strcmp(line, "ab") == 0, "backspace removes a whole 3-byte character");

    n = run(&le, "ab💧\x08\x08x\n", line);
    CHECK(strcmp(line, "ax") == 0, "backspace over 4-byte emoji then ASCII");

    n = run(&le, "\x7f\x7f\n", line);
    CHECK(n == 0 && line[0] == 0, "backspace on an empty line is harmless");

    n = run(&le, "hello\x15world\n", line);
    CHECK(strcmp(line, "world") == 0, "Ctrl-U clears the line");

    n = run(&le, "junk\x03", line);
    CHECK(n == 0 && strstr(s_out, "^C") != NULL, "Ctrl-C abandons the line");

    n = run(&le, "wifi a b\r\n", line);
    CHECK(strcmp(line, "wifi a b") == 0, "CR LF ends the line once");
    n = run(&le, "say 1\n", line);
    CHECK(strcmp(line, "say 1") == 0, "next line unaffected after CR LF");

    /* history: lines so far include "say 1", "wifi a b", ... */
    n = run(&le, "\x1b[A\n", line);
    CHECK(strcmp(line, "say 1") == 0, "up arrow recalls the last command");
    n = run(&le, "\x1b[A\x1b[A\n", line);
    CHECK(strcmp(line, "wifi a b") == 0, "two up arrows recall the one before");
    n = run(&le, "typed\x1b[A\x1b[B\n", line);
    CHECK(strcmp(line, "typed") == 0, "down arrow returns to what was being typed");
    n = run(&le, "msg 你好\n", line);
    n = run(&le, "\x1b[A\n", line);
    CHECK(strcmp(line, "msg 你好") == 0, "Chinese survives history");

    n = run(&le, "a\x1b[Cb\x1b[3~c\x1bOHd\n", line);
    CHECK(strcmp(line, "abcd") == 0, "other escape sequences are swallowed");

    n = run(&le, "x\ty\n", line);
    CHECK(strcmp(line, "xy") == 0, "TAB ignored");

    n = run(&le, "\n", line);
    CHECK(n == 0, "empty line returns 0");

    n = run(&le, "abc", line);
    CHECK(n == -1, "EOF returns -1");

    /* overlong input is truncated, not overflowed */
    char big[600];
    memset(big, 'z', sizeof big - 2);
    big[sizeof big - 2] = '\n';
    big[sizeof big - 1] = 0;
    n = run(&le, big, line);
    CHECK(n == LINEEDIT_MAX - 1 && line[n] == 0, "overlong line truncated to the buffer");

    /* echo shows redraw on backspace */
    n = run(&le, "ab想\x7f\n", line);
    CHECK(strstr(s_out, "\r\x1b[K" "ke-body> " "ab") != NULL, "backspace redraws the line with the prompt");

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
