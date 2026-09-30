#include "games.h"
#include <math.h>
#include <string.h>

uint32_t game_rand(uint32_t *st)
{
    uint32_t x = *st ? *st : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *st = x;
    return x;
}

/* ---- memory ----------------------------------------------------------------------------------------------------- */

void memory_new(memory_t *m, uint32_t seed)
{
    memset(m, 0, sizeof *m);
    m->rng = seed ? seed : 1;
    m->first = -1;
    for (int i = 0; i < MEM_CARDS; i++) m->kind[i] = (uint8_t)(i / 2);
    for (int i = MEM_CARDS - 1; i > 0; i--) {                      /* shuffle */
        int j = (int)(game_rand(&m->rng) % (uint32_t)(i + 1));
        uint8_t t = m->kind[i]; m->kind[i] = m->kind[j]; m->kind[j] = t;
    }
}

mem_event_t memory_tap(memory_t *m, int c)
{
    if (c < 0 || c >= MEM_CARDS || m->won || m->hide_ms > 0 || m->up[c] || m->matched[c]) return MEM_NONE;
    m->started = true;
    m->up[c] = true;
    if (m->first < 0) { m->first = c; return MEM_FIRST; }
    m->moves++;
    if (m->kind[m->first] == m->kind[c]) {
        m->matched[m->first] = m->matched[c] = true;
        m->first = -1;
        if (++m->found == MEM_KINDS) { m->won = true; return MEM_WIN; }
        return MEM_MATCH;
    }
    m->hide_ms = MEM_HIDE_MS;
    return MEM_MISMATCH;
}

void memory_tick(memory_t *m, int ms)
{
    if (m->started && !m->won) m->elapsed_ms += ms;
    if (m->hide_ms > 0) {
        m->hide_ms -= ms;
        if (m->hide_ms <= 0) {
            m->hide_ms = 0;
            for (int i = 0; i < MEM_CARDS; i++) if (m->up[i] && !m->matched[i]) m->up[i] = false;
            m->first = -1;
        }
    }
}

/* ---- 2048 ----------------------------------------------------------------------------------------------------------- */

static int count_empty(const g2048_t *g)
{
    int n = 0;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) if (!g->cell[y][x]) n++;
    return n;
}

static void spawn(g2048_t *g)
{
    int n = count_empty(g);
    if (n == 0) return;
    int k = (int)(game_rand(&g->rng) % (uint32_t)n);
    int v = (int)(game_rand(&g->rng) % 100) < G2048_FOUR_PERCENT ? 4 : 2;
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            if (!g->cell[y][x] && k-- == 0) { g->cell[y][x] = v; return; }
}

static bool can_move(const g2048_t *g)
{
    if (count_empty(g)) return true;
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
            if (x < 3 && g->cell[y][x] == g->cell[y][x + 1]) return true;
            if (y < 3 && g->cell[y][x] == g->cell[y + 1][x]) return true;
        }
    return false;
}

static void note_best(g2048_t *g)
{
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) if (g->cell[y][x] > g->best_tile) g->best_tile = g->cell[y][x];
}

void g2048_new(g2048_t *g, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 1;
    spawn(g);
    spawn(g);
    note_best(g);
}

/* slide one line of four towards index 0; returns the score gained */
static int slide_line(int v[4], bool *changed)
{
    int out[4] = {0}, n = 0, gain = 0;
    for (int i = 0; i < 4; i++) {
        if (!v[i]) continue;
        if (n > 0 && out[n - 1] == v[i] && out[n - 1] > 0) {
            out[n - 1] *= -2;                                  /* merged this move: negative marks "do not merge again" */
            gain += -out[n - 1];
        } else {
            out[n++] = v[i];
        }
    }
    for (int i = 0; i < 4; i++) {
        int o = out[i] < 0 ? -out[i] : out[i];
        if (v[i] != o) *changed = true;
        v[i] = o;
    }
    return gain;
}

bool g2048_move(g2048_t *g, int dir)
{
    g->undone_now = false;
    if (g->over) return false;
    int before[4][4];
    memcpy(before, g->cell, sizeof before);
    int score_before = g->score;
    bool changed = false;
    for (int i = 0; i < 4; i++) {
        int line[4];
        for (int k = 0; k < 4; k++) {                          /* line[0] is the cell the tiles slide towards */
            switch (dir) {
            case G2048_LEFT:  line[k] = g->cell[i][k]; break;
            case G2048_RIGHT: line[k] = g->cell[i][3 - k]; break;
            case G2048_UP:    line[k] = g->cell[k][i]; break;
            default:          line[k] = g->cell[3 - k][i]; break;
            }
        }
        g->score += slide_line(line, &changed);
        for (int k = 0; k < 4; k++) {
            switch (dir) {
            case G2048_LEFT:  g->cell[i][k] = line[k]; break;
            case G2048_RIGHT: g->cell[i][3 - k] = line[k]; break;
            case G2048_UP:    g->cell[k][i] = line[k]; break;
            default:          g->cell[3 - k][i] = line[k]; break;
            }
        }
    }
    if (!changed) { g->score = score_before; return false; }
    memcpy(g->prev, before, sizeof before);
    g->prev_score = score_before;
    g->has_prev = true;
    spawn(g);
    note_best(g);
    if (!can_move(g)) {
        if (!g->undo_used) {                                   /* one free take-back instead of a game over */
            memcpy(g->cell, g->prev, sizeof g->cell);
            g->score = g->prev_score;
            g->undo_used = true;
            g->has_prev = false;
            g->undone_now = true;
        } else {
            g->over = true;
        }
    }
    return true;
}

/* ---- bubbles ------------------------------------------------------------------------------------------------------------ */

static float frand(uint32_t *st, float lo, float hi) { return lo + (hi - lo) * (float)(game_rand(st) % 10000) / 10000.0f; }

void bubbles_new(bubbles_t *g, int w, int h, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->w = w;
    g->h = h;
    g->rng = seed ? seed : 1;
    g->time_left_ms = BUB_GAME_MS;
    g->spawn_ms = 200;
}

float bubble_x(const bubble_t *b) { return b->x; }

static void spawn_bubble(bubbles_t *g)
{
    for (int i = 0; i < BUB_MAX; i++) {
        bubble_t *b = &g->b[i];
        if (b->alive || b->pop_ms > 0) continue;
        memset(b, 0, sizeof *b);
        b->r = frand(&g->rng, 20.f, 36.f);
        b->x = frand(&g->rng, b->r + 6.f, (float)g->w - b->r - 6.f);
        b->y = (float)g->h + b->r;
        float speed_up = 1.0f + 0.5f * (float)(BUB_GAME_MS - g->time_left_ms) / BUB_GAME_MS;     /* a bit faster as the game goes on */
        b->vy = frand(&g->rng, 55.f, 110.f) * speed_up;
        b->phase = frand(&g->rng, 0.f, 6.28f);
        b->sway = frand(&g->rng, 0.f, 14.f);
        b->crab = game_rand(&g->rng) % 9 == 0;
        b->alive = true;
        return;
    }
}

void bubbles_tick(bubbles_t *g, int ms)
{
    if (g->over) return;
    float dt = (float)ms / 1000.f;
    g->clock += dt;
    g->time_left_ms -= ms;
    if (g->time_left_ms <= 0) { g->time_left_ms = 0; g->over = true; }
    for (int i = 0; i < BUB_MAX; i++) {
        bubble_t *b = &g->b[i];
        if (b->pop_ms > 0) { b->pop_ms -= ms; if (b->pop_ms < 0) b->pop_ms = 0; }
        if (!b->alive) continue;
        b->y -= b->vy * dt;
        b->x += b->sway * 0.6f * sinf(g->clock * 1.8f + b->phase) * dt;             /* drift sideways a little */
        if (b->x < b->r) b->x = b->r;
        if (b->x > (float)g->w - b->r) b->x = (float)g->w - b->r;
        if (b->y < -b->r) b->alive = false;
    }
    g->spawn_ms -= ms;
    if (g->spawn_ms <= 0 && !g->over) {
        spawn_bubble(g);
        g->spawn_ms = 240 + (int)(game_rand(&g->rng) % 360);
    }
}

int bubbles_tap(bubbles_t *g, int x, int y)
{
    if (g->over) return 0;
    int best = -1;
    float best_d = 1e9f;
    for (int i = 0; i < BUB_MAX; i++) {
        const bubble_t *b = &g->b[i];
        if (!b->alive) continue;
        float dx = (float)x - b->x, dy = (float)y - b->y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d <= b->r + 10.f && d / b->r < best_d) { best = i; best_d = d / b->r; }      /* a generous finger: 10 px around the bubble */
    }
    if (best < 0) return 0;
    bubble_t *b = &g->b[best];
    b->alive = false;
    b->pop_ms = BUB_POP_MS;
    int pts = b->crab ? 5 : 1;
    g->score += pts;
    g->pops++;
    if (b->crab) g->crabs++;
    return pts;
}
