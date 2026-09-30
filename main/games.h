/* The three little games (pure C, host-testable): memory pairs, 2048 and bubble popping. No drawing here. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* ---- memory pairs: 12 cards, 6 pairs ---------------------------------------------------------------------------- */

#define MEM_CARDS 12
#define MEM_KINDS 6
#define MEM_HIDE_MS 750             /* a mismatched pair stays open this long */

typedef struct {
    uint8_t kind[MEM_CARDS];        /* which of the 6 faces (see MEM_FACES in ui_render.c) */
    bool up[MEM_CARDS];             /* face up right now */
    bool matched[MEM_CARDS];
    int first;                      /* first card of the pair being tried, -1 */
    int hide_ms;                    /* > 0: a mismatched pair is showing, it turns back when this runs out */
    int moves;                      /* pairs tried */
    int elapsed_ms;                 /* runs from the first tap to the last pair */
    int found;
    bool started, won;
    uint32_t rng;
} memory_t;

typedef enum { MEM_NONE = 0, MEM_FIRST, MEM_MATCH, MEM_MISMATCH, MEM_WIN } mem_event_t;

void memory_new(memory_t *m, uint32_t seed);
mem_event_t memory_tap(memory_t *m, int card);       /* MEM_NONE when the tap is ignored */
void memory_tick(memory_t *m, int ms);

/* ---- 2048 ---------------------------------------------------------------------------------------------------------- */

typedef struct {
    int cell[4][4];                 /* 0 or the tile value */
    int prev[4][4];                 /* the board before the last move (for the one free undo) */
    int score, prev_score;
    bool has_prev, undo_used, over, undone_now;
    int best_tile;
    uint32_t rng;
} g2048_t;

enum { G2048_UP = 0, G2048_DOWN, G2048_LEFT, G2048_RIGHT };
#define G2048_FOUR_PERCENT 20       /* chance that a new tile is a 4 (the standard game uses 10) */

void g2048_new(g2048_t *g, uint32_t seed);
/* returns true when the board changed. After a move that leaves no move at all, the first time the move is taken back
 * automatically (undone_now = true, over stays false); the second time over = true. */
bool g2048_move(g2048_t *g, int dir);

/* ---- bubbles ---------------------------------------------------------------------------------------------------------- */

#define BUB_MAX 16
#define BUB_GAME_MS 30000
#define BUB_POP_MS 180

typedef struct {
    float x, y, r, vy, phase, sway;
    bool crab;
    bool alive;
    int pop_ms;                     /* > 0: popping animation */
} bubble_t;

typedef struct {
    bubble_t b[BUB_MAX];
    int w, h;                       /* play field */
    int score, pops, crabs;
    int time_left_ms;
    int spawn_ms;
    bool over;
    uint32_t rng;
    float clock;
} bubbles_t;

void bubbles_new(bubbles_t *g, int w, int h, uint32_t seed);
void bubbles_tick(bubbles_t *g, int ms);
int bubbles_tap(bubbles_t *g, int x, int y);         /* points for this tap: 0 miss, 1 bubble, 5 crab bubble */
float bubble_x(const bubble_t *b);                    /* drawn x (with the sideways sway) */

/* ---- shared ---------------------------------------------------------------------------------------------------------------- */
uint32_t game_rand(uint32_t *state);                  /* xorshift32 */
