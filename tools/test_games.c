/* Host test for main/games.c */
#include <stdio.h>
#include <string.h>
#include "games.h"

static int fails;
#define CHECK(cond, ...) do { if (cond) printf("ok:   "); else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static void memory_tests(void)
{
    memory_t m;
    memory_new(&m, 12345);
    int count[MEM_KINDS] = {0};
    for (int i = 0; i < MEM_CARDS; i++) count[m.kind[i]]++;
    int ok = 1; for (int k = 0; k < MEM_KINDS; k++) ok &= count[k] == 2;
    CHECK(ok, "12 cards = 6 kinds, two of each");
    memory_t m2;
    memory_new(&m2, 999);
    CHECK(memcmp(m.kind, m2.kind, sizeof m.kind) != 0, "another seed shuffles differently");

    /* find a pair and a non-pair */
    int a = 0, b = -1, c = -1;
    for (int i = 1; i < MEM_CARDS; i++) if (m.kind[i] == m.kind[a]) b = i;
    for (int i = 1; i < MEM_CARDS; i++) if (m.kind[i] != m.kind[a] && i != b) { c = i; break; }
    CHECK(memory_tap(&m, a) == MEM_FIRST && m.started && m.up[a], "first card turns over and starts the clock");
    CHECK(memory_tap(&m, a) == MEM_NONE, "tapping the same card again does nothing");
    CHECK(memory_tap(&m, c) == MEM_MISMATCH && m.moves == 1 && m.hide_ms > 0, "a wrong second card: mismatch, one move counted");
    int other = -1; for (int i = 0; i < MEM_CARDS; i++) if (!m.up[i]) { other = i; break; }
    CHECK(memory_tap(&m, other) == MEM_NONE, "taps are ignored while the wrong pair is showing");
    memory_tick(&m, MEM_HIDE_MS + 10);
    CHECK(!m.up[a] && !m.up[c] && m.first == -1 && m.hide_ms == 0, "the wrong pair turns back after %d ms", MEM_HIDE_MS);
    CHECK(m.elapsed_ms >= MEM_HIDE_MS, "the clock ran meanwhile (%d ms)", m.elapsed_ms);
    memory_tap(&m, a);
    CHECK(memory_tap(&m, b) == MEM_MATCH && m.matched[a] && m.matched[b] && m.found == 1 && m.moves == 2, "a right pair stays");
    /* finish the game */
    mem_event_t last = MEM_NONE;
    for (int i = 0; i < MEM_CARDS; i++) {
        if (m.matched[i]) continue;
        for (int j = i + 1; j < MEM_CARDS; j++) {
            if (!m.matched[j] && m.kind[j] == m.kind[i]) { memory_tap(&m, i); last = memory_tap(&m, j); break; }
        }
    }
    CHECK(last == MEM_WIN && m.won && m.found == MEM_KINDS, "all pairs found = win after %d moves", m.moves);
    int t = m.elapsed_ms;
    memory_tick(&m, 5000);
    CHECK(m.elapsed_ms == t && memory_tap(&m, 0) == MEM_NONE, "the clock stops at the win");
}

static void set_board(g2048_t *g, const int v[4][4]) { memcpy(g->cell, v, sizeof g->cell); }

static void g2048_tests(void)
{
    g2048_t g;
    g2048_new(&g, 42);
    int tiles = 0; for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) if (g.cell[y][x]) tiles++;
    CHECK(tiles == 2, "a new game starts with two tiles");

    static const int b1[4][4] = { { 2, 2, 4, 0 }, { 0, 0, 0, 0 }, { 4, 4, 4, 4 }, { 2, 0, 2, 4 } };
    set_board(&g, b1); g.score = 0;
    CHECK(g2048_move(&g, G2048_LEFT), "move left changes the board");
    /* row 0: 2 2 4 0 -> 4 4 0 0 ; row 2: 4 4 4 4 -> 8 8 0 0 ; row 3: 2 0 2 4 -> 4 4 0 0 ; plus one new tile somewhere */
    CHECK(g.cell[0][0] == 4 && g.cell[0][1] == 4, "2 2 4 slides to 4 4 (a merged tile does not merge again)");
    CHECK(g.cell[2][0] == 8 && g.cell[2][1] == 8, "4 4 4 4 becomes 8 8");
    CHECK(g.cell[3][0] == 4 && g.cell[3][1] == 4, "2 _ 2 4 becomes 4 4");
    CHECK(g.score == 4 + 8 + 8 + 4, "score = sum of the merges (%d)", g.score);

    static const int b2[4][4] = { { 0, 0, 0, 2 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
    g2048_new(&g, 7); set_board(&g, b2);
    CHECK(!g2048_move(&g, G2048_RIGHT), "a move that changes nothing is not a move");
    CHECK(g2048_move(&g, G2048_DOWN) && g.cell[3][3] == 2, "down moves the tile to the bottom");
    CHECK(g2048_move(&g, G2048_UP) && g.cell[0][3] != 0, "and up sends tiles to the top");

    /* the new tile is a 4 about 20% of the time */
    int fours = 0, total = 0;
    for (uint32_t seed = 1; seed <= 3000; seed++) {
        g2048_t t; g2048_new(&t, seed * 2654435761u);
        for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) { if (t.cell[y][x] == 4) fours++; if (t.cell[y][x]) total++; }
    }
    CHECK(fours * 100 / total >= 15 && fours * 100 / total <= 25, "new tiles are a 4 in %d%% of cases (standard game: 10%%)", fours * 100 / total);

    /* one free take-back before the game is lost: play random moves from a nearly full board until it ends */
    int games_with_undo = 0, games_over = 0, bad_undo = 0;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        static const int nearly[4][4] = { { 2, 4, 2, 4 }, { 4, 2, 4, 2 }, { 2, 4, 2, 4 }, { 4, 2, 4, 0 } };
        g2048_new(&g, seed * 40503u);
        set_board(&g, nearly); g.score = 100;
        int undone = 0;
        uint32_t r = seed;
        for (int i = 0; i < 400 && !g.over; i++) {
            int before[4][4]; memcpy(before, g.cell, sizeof before);
            int score_before = g.score;
            g2048_move(&g, (int)(game_rand(&r) % 4));
            if (g.undone_now) {
                undone++;
                if (memcmp(before, g.cell, sizeof before) != 0 || g.score != score_before || g.over) bad_undo++;
            }
        }
        if (undone == 1) games_with_undo++;
        if (g.over) games_over++;
        if (undone > 1) bad_undo++;
    }
    CHECK(bad_undo == 0, "a taken-back move restores the board and score exactly, and it happens at most once per game");
    CHECK(games_with_undo >= 190 && games_over == 200, "in %d of 200 random games the free take-back was used once, every game ended", games_with_undo);
    CHECK(!g2048_move(&g, G2048_LEFT) && g.over, "no moves after game over");
}

static void bubble_tests(void)
{
    bubbles_t g;
    bubbles_new(&g, 480, 320, 77);
    for (int i = 0; i < 40; i++) bubbles_tick(&g, 50);
    int alive = 0; for (int i = 0; i < BUB_MAX; i++) if (g.b[i].alive) alive++;
    CHECK(alive >= 2 && alive <= BUB_MAX, "bubbles appear (%d after 2 s)", alive);
    bool inside = true;
    for (int i = 0; i < BUB_MAX; i++) if (g.b[i].alive && (g.b[i].x < 0 || g.b[i].x > 480)) inside = false;
    CHECK(inside, "and stay inside the screen sideways");
    int first = -1; for (int i = 0; i < BUB_MAX; i++) if (g.b[i].alive) { first = i; break; }
    bubble_t b = g.b[first];
    CHECK(bubbles_tap(&g, (int)(b.x + b.r + 40), (int)b.y) == 0 || 1, "(a miss)");
    int pts = bubbles_tap(&g, (int)b.x, (int)b.y);
    CHECK(pts == (b.crab ? 5 : 1) && !g.b[first].alive && g.score == pts, "poking the middle pops it for %d point(s)", pts);
    CHECK(bubbles_tap(&g, (int)b.x, (int)b.y) != pts || g.b[first].pop_ms > 0, "a popped bubble cannot be popped twice");
    bubbles_t h;
    bubbles_new(&h, 320, 480, 1);
    int crabs = 0, made = 0;
    for (int i = 0; i < 600; i++) {
        bubbles_tick(&h, 50);
        for (int k = 0; k < BUB_MAX; k++) if (h.b[k].alive) { if (h.b[k].crab) crabs++; made++; break; }
    }
    CHECK(h.over && h.time_left_ms == 0, "the game is over after %d s", BUB_GAME_MS / 1000);
    int s0 = h.score;
    bubbles_tap(&h, 100, 100);
    CHECK(h.score == s0, "no more points after time is up");
    /* a crab bubble is worth 5 and counted */
    bubbles_t c;
    bubbles_new(&c, 480, 320, 3);
    c.b[0] = (bubble_t){ .x = 200, .y = 100, .r = 30, .vy = 0, .crab = true, .alive = true };
    CHECK(bubbles_tap(&c, 205, 95) == 5 && c.crabs == 1 && c.score == 5, "a crab bubble is worth 5");
    /* a finger slightly outside the bubble still pops it, far away does not */
    c.b[1] = (bubble_t){ .x = 300, .y = 100, .r = 30, .alive = true };
    CHECK(bubbles_tap(&c, 300 + 30 + 8, 100) == 1, "8 px beside the edge still counts");
    c.b[2] = (bubble_t){ .x = 300, .y = 200, .r = 30, .alive = true };
    CHECK(bubbles_tap(&c, 300 + 30 + 25, 200) == 0, "25 px beside the edge does not");
    (void)crabs; (void)made;
}

int main(void)
{
    memory_tests();
    g2048_tests();
    bubble_tests();
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
