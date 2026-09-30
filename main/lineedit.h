/* Minimal UTF-8 aware line editor for the serial console.
 *
 * Why: the IDF linenoise strips every byte >= 0x80 (sanitize() uses isprint()),
 * so Chinese text typed at the console arrives as an empty string. This editor
 * passes bytes through untouched, deletes whole UTF-8 characters on backspace,
 * and keeps a small history. It needs an ANSI terminal (PuTTY, Tera Term,
 * Windows Terminal, miniterm all are).
 *
 * Pure C, no ESP-IDF dependency: tools/test_lineedit.c exercises it on the host. */
#pragma once
#include <stddef.h>

#define LINEEDIT_MAX    256   /* max line length including the terminating NUL */
#define LINEEDIT_HIST_N 16

typedef struct {
    int (*read)(void);                        /* blocks; returns a byte 0..255, or -1 for EOF (tests) */
    void (*write)(const char *s, size_t n);   /* echo to the terminal */
    const char *prompt;
    char hist[LINEEDIT_HIST_N][LINEEDIT_MAX];
    int hist_n, hist_next;
    int prev;                                 /* previous input byte, to fold CR LF */
} lineedit_t;

/* Reads one line into buf (size >= LINEEDIT_MAX recommended). Returns its length
 * (0 for an empty line or after Ctrl-C), or -1 on EOF. */
int lineedit_read(lineedit_t *le, char *buf, size_t size);
