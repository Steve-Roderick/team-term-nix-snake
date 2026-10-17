#include "game.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int same(Point a, Point b) { return a.x == b.x && a.y == b.y; }
static direction opposite(direction d) {
    static const direction dirs[] = { DOWN, UP, RIGHT, LEFT };
    return dirs[d];
}
static Point step(Point p, direction d) {
    if (d == UP) --p.y;
    if (d == DOWN) ++p.y;
    if (d == LEFT) --p.x;
    if (d == RIGHT) ++p.x;
    if (p.x == 0) p.x = WIDTH - 2;
    if (p.x == WIDTH - 1) p.x = 1;
    if (p.y == 0) p.y = HEIGHT - 2;
    if (p.y == HEIGHT - 1) p.y = 1;
    return p;
}
static void clear_map(Game *g) {
    memset(g->terrain, 0, sizeof(g->terrain));
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x)
            if (!y || !x || y == HEIGHT - 1 || x == WIDTH - 1)
                g->terrain[y][x] = BORDER;
}
void game_render(const Game *g, int map[HEIGHT][WIDTH]) {
    memcpy(map, g->terrain, sizeof(g->terrain));
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        const Player *p = &g->players[i];
        if (p->active && p->alive)
            for (int b = 0; b < p->length; ++b)
                map[p->body[b].y][p->body[b].x] = i + 1;
    }
}
/* Reserve current bodies and straight-ahead routes for ten ticks, allowing
   two steps per tick in case an apple boosts either snake. Search is bounded;
   a crowded arena delays respawn rather than placing a snake in danger. */
static int spawn(Game *g, Player *p) {
    int map[HEIGHT][WIDTH];
    game_render(g, map);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        const Player *other = &g->players[i];
        if (!other->active || !other->alive) continue;
        Point ahead = other->body[0];
        for (int t = 0; t < 2 * (SPAWN_CLEAR_TICKS + 1); ++t) {
            ahead = step(ahead, other->pending_dir);
            map[ahead.y][ahead.x] = i + 1;
        }
    }
    int start = rand() % MAX_LENGTH, first_dir = rand() % 4;
    for (int k = 0; k < MAX_LENGTH; ++k) {
        int cell = (start + k) % MAX_LENGTH;
        Point head = { cell % (WIDTH - 2) + 1, cell / (WIDTH - 2) + 1 };
        for (int trial = 0; trial < 4; ++trial) {
            direction d = (direction)((first_dir + trial) % 4);
            Point body[SNK_START_LEN], pos = head;
            int safe = 1;
            for (int b = 0; b < SNK_START_LEN; ++b) {
                if (map[pos.y][pos.x] != EMPTY) { safe = 0; break; }
                body[b] = pos;
                pos = step(pos, opposite(d));
            }
            if (!safe) continue;
            pos = head;
            for (int t = 0; t < 2 * SPAWN_CLEAR_TICKS && safe; ++t) {
                pos = step(pos, d);
                if (map[pos.y][pos.x] != EMPTY) safe = 0;
                for (int b = 0; b < SNK_START_LEN; ++b)
                    if (same(pos, body[b])) safe = 0;
            }
            if (!safe) continue;
            memcpy(p->body, body, sizeof(body));
            p->length = SNK_START_LEN;
            p->alive = 1;
            p->dir = p->pending_dir = d;
            p->boost = p->pacman = p->respawn = 0;
            return 1;
        }
    }
    return 0;
}
static void replenish(Game *g) {
    int map[HEIGHT][WIDTH], apples = 0, berries = 0, count = 0;
    Point empty[MAX_LENGTH];
    game_render(g, map);
    for (int y = 1; y < HEIGHT - 1; ++y)
        for (int x = 1; x < WIDTH - 1; ++x) {
            apples += g->terrain[y][x] == FRUIT;
            berries += g->terrain[y][x] == BERRY;
            if (map[y][x] == EMPTY) empty[count++] = (Point){x, y};
        }
    while (count && (apples < APPLE_COUNT || berries < BERRY_COUNT)) {
        int index = rand() % count;
        Point cell = empty[index];
        empty[index] = empty[--count];
        if (apples < APPLE_COUNT) { g->terrain[cell.y][cell.x] = FRUIT; ++apples; }
        else { g->terrain[cell.y][cell.x] = BERRY; ++berries; }
    }
}
void game_init(Game *g) {
    memset(g, 0, sizeof(*g));
    g->host = g->winner = -1;
    g->tps = DEFAULT_TPS;
    clear_map(g);
}
int game_join(Game *g, const char *name) {
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player *p = &g->players[i];
        if (p->active) continue;
        memset(p, 0, sizeof(*p));
        p->active = 1;
        p->color = i + 1;
        snprintf(p->name, sizeof(p->name), "%s", name[0] ? name : "Worm");
        if (g->host < 0) g->host = i;
        if (g->state == STATE_RUNNING && g->mode == MODE_MAX_SCORE) {
            p->participating = 1;
            if (!spawn(g, p)) p->respawn = 1;
        }
        return i;
    }
    return -1;
}
static void check_winner(Game *g) {
    if (g->state != STATE_RUNNING || g->countdown) return;
    int alive = 0, last = -1, best = -1, tied = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player *p = &g->players[i];
        if (!p->active || !p->participating) continue;
        if (p->alive) { ++alive; last = i; }
        if (p->score >= WIN_SCORE) {
            if (best < 0 || p->score > g->players[best].score) { best = i; tied = 0; }
            else if (p->score == g->players[best].score) tied = 1;
        }
    }
    if (g->mode == MODE_LAST_STANDING && (alive == 0 || (g->starters > 1 && alive == 1))) {
        g->state = STATE_GAME_OVER;
        g->winner = last;
    } else if (g->mode == MODE_MAX_SCORE && best >= 0) {
        g->state = STATE_GAME_OVER;
        g->winner = tied ? -1 : best;
    }
    if (g->state == STATE_GAME_OVER && g->winner >= 0)
        memcpy(g->winner_name, g->players[g->winner].name, NAME_SIZE);
}
void game_leave(Game *g, int id) {
    if (id < 0 || id >= MAX_PLAYERS) return;
    memset(&g->players[id], 0, sizeof(Player));
    if (g->host == id) {
        g->host = -1;
        for (int i = 0; i < MAX_PLAYERS; ++i)
            if (g->players[i].active) { g->host = i; break; }
    }
    if (g->host < 0) {
        g->state = STATE_WAITING;
        g->paused = g->countdown = 0;
        g->winner = -1;
        g->winner_name[0] = '\0';
        clear_map(g);
    } else check_winner(g);
}
static void start_round(Game *g) {
    clear_map(g);
    g->winner = -1;
    g->winner_name[0] = '\0';
    g->paused = 0;
    g->tick = 0;
    g->countdown = COUNTDOWN_TICKS;
    g->starters = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player *p = &g->players[i];
        p->alive = p->length = p->score = p->boost = p->pacman = p->respawn = 0;
        p->participating = p->active;
    }
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player *p = &g->players[i];
        if (p->active) { if (!spawn(g, p)) p->respawn = 1; ++g->starters; }
    }
    replenish(g);
    g->state = STATE_RUNNING;
}
void game_input(Game *g, int id, unsigned char key) {
    if (id < 0 || id >= MAX_PLAYERS || !g->players[id].active) return;
    Player *p = &g->players[id];
    if (key == QUIT_KEY) { game_leave(g, id); return; }
    if (g->state != STATE_RUNNING) {
        if (key >= '1' && key <= '8') p->color = key - '0';
        if (key == MODE_KEY && id == g->host) g->mode = !g->mode;
        if (key == START_KEY && id == g->host) start_round(g);
    } else if (key == PAUSE_KEY) g->paused = !g->paused;
    if (key == '+' || key == '=') { if (g->tps < MAX_TPS) ++g->tps; }
    if (key == '-') { if (g->tps > MIN_TPS) --g->tps; }
    if (g->state == STATE_RUNNING && p->alive) {
        direction d;
        switch (key) {
            case UP_KEY: d = UP; break;
            case DOWN_KEY: d = DOWN; break;
            case LEFT_KEY: d = LEFT; break;
            case RIGHT_KEY: d = RIGHT; break;
            default: return;
        }
        if (d != opposite(p->dir)) p->pending_dir = d;
    }
}
static void gain(Player *p, int amount) {
    if (amount <= 0) return;
    p->score = p->score > INT_MAX - amount ? INT_MAX : p->score + amount;
    Point tail = p->body[p->length - 1];
    int end = p->length + amount;
    if (end > MAX_LENGTH) end = MAX_LENGTH;
    while (p->length < end) p->body[p->length++] = tail;
}
static void die(Game *g, Player *p) {
    p->alive = 0;
    p->length = p->boost = p->pacman = 0;
    p->score -= p->score / 4 + (p->score % 4 != 0);
    p->respawn = g->mode == MODE_MAX_SCORE ? RESPAWN_TICKS : 0;
}
/* Reverse the remaining body so a bitten snake moves away from contact. */
static void bounce(Player *p) {
    for (int a = 0, b = p->length - 1; a < b; ++a, --b) {
        Point tmp = p->body[a]; p->body[a] = p->body[b]; p->body[b] = tmp;
    }
    direction d = opposite(p->dir);
    for (int b = 1; b < p->length; ++b) {
        if (same(p->body[0], p->body[b])) continue;
        for (int candidate = UP; candidate <= RIGHT; ++candidate)
            if (same(step(p->body[b], (direction)candidate), p->body[0])) {
                d = (direction)candidate;
                break;
            }
        break;
    }
    p->dir = p->pending_dir = d;
}
static void move_step(Game *g, const int moving[MAX_PLAYERS],
                      unsigned char contacted[MAX_PLAYERS][MAX_PLAYERS], int berry_eaten[MAX_PLAYERS]) {
    Point next[MAX_PLAYERS];
    int fatal[MAX_PLAYERS] = {0}, blocked[MAX_PLAYERS] = {0};
    int hit[MAX_PLAYERS][MAX_PLAYERS] = {{0}}, old_length[MAX_PLAYERS];
    int loss[MAX_PLAYERS] = {0}, earned[MAX_PLAYERS] = {0};
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player *p = &g->players[i];
        old_length[i] = p->length;
        next[i] = p->body[0];
        if (!p->active || !p->alive || !moving[i]) continue;
        next[i] = step(p->body[0], p->dir);
        /* The tail vacates this step unless eating (growth retains it). */
        int keep_tail = g->terrain[next[i].y][next[i].x] == FRUIT;
        for (int b = 0; b < p->length - !keep_tail; ++b)
            if (same(next[i], p->body[b])) fatal[i] = 1;
    }
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player *p = &g->players[i];
        if (!p->active || !p->alive || !moving[i] || fatal[i]) continue;
        for (int j = 0; j < MAX_PLAYERS; ++j) {
            Player *q = &g->players[j];
            if (i == j || !q->active || !q->alive || fatal[j]) continue;
            for (int b = 0; b < q->length; ++b)
                if (same(next[i], q->body[b])) hit[i][j] = 1;
            if (moving[j] && same(next[i], next[j])) hit[i][j] = hit[j][i] = 1;
        }
    }
    /* Resolve contacts from a snapshot so slot order cannot grant an advantage.
       Each snake loses at most half per substep, split among simultaneous eaters. */
    int eats[MAX_PLAYERS][MAX_PLAYERS] = {{0}};
    for (int i = 0; i < MAX_PLAYERS; ++i) for (int j = i + 1; j < MAX_PLAYERS; ++j) {
        if (!hit[i][j] && !hit[j][i]) continue;
        blocked[i] = blocked[j] = 1;
        if (contacted[i][j]) continue;
        contacted[i][j] = contacted[j][i] = 1;
        int pi = g->players[i].pacman > 0, pj = g->players[j].pacman > 0;
        if (pi != pj) eats[pi ? i : j][pi ? j : i] = 1;
        else if (hit[i][j] && hit[j][i]) { loss[i] = loss[j] = 1; }
        else if (hit[i][j]) eats[j][i] = 1;
        else eats[i][j] = 1;
    }
    for (int victim = 0; victim < MAX_PLAYERS; ++victim) {
        int eaters = 0;
        for (int i = 0; i < MAX_PLAYERS; ++i) eaters += eats[i][victim];
        if (!eaters && !loss[victim]) continue;
        loss[victim] = old_length[victim] / 2;
        if (old_length[victim] == 1) fatal[victim] = 1;
        if (eaters) {
            /* Equal shares; discard indivisible remainder to avoid slot bias. */
            for (int i = 0; i < MAX_PLAYERS; ++i)
                if (eats[i][victim]) earned[i] += loss[victim] / eaters;
        }
    }
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player *p = &g->players[i];
        if (!p->active || !p->alive) continue;
        if (fatal[i]) { die(g, p); continue; }
        if (loss[i]) { p->length -= loss[i]; bounce(p); }
        gain(p, earned[i]);
        if (!moving[i] || blocked[i]) continue;
        int food = g->terrain[next[i].y][next[i].x];
        if (food == FRUIT) { gain(p, SNK_FRUIT_LEN); p->boost = BOOST_TICKS; }
        if (food == BERRY) { p->pacman = PACMAN_TICKS; berry_eaten[i] = 1; }
        if (food == FRUIT || food == BERRY) g->terrain[next[i].y][next[i].x] = EMPTY;
        memmove(p->body + 1, p->body, (size_t)(p->length - 1) * sizeof(Point));
        p->body[0] = next[i];
    }
}
void game_tick(Game *g) {
    if (g->state != STATE_RUNNING || g->paused) return;
    if (g->countdown) { --g->countdown; check_winner(g); return; }
    ++g->tick;
    int moving[MAX_PLAYERS] = {0}, fast[MAX_PLAYERS] = {0}, berry_eaten[MAX_PLAYERS] = {0};
    unsigned char contacted[MAX_PLAYERS][MAX_PLAYERS] = {{0}};
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player *p = &g->players[i];
        if (!p->active || !p->participating) continue;
        if (!p->alive) {
            if (g->mode == MODE_MAX_SCORE && p->respawn > 0 && --p->respawn == 0)
                if (!spawn(g, p)) p->respawn = 1;
            continue; /* A respawn first moves on the following tick. */
        }
        moving[i] = 1; fast[i] = p->boost > 0;
        p->dir = p->pending_dir;
        /* Eating again refreshes all 10 future ticks. */
        if (p->boost) --p->boost;
    }
    move_step(g, moving, contacted, berry_eaten);
    move_step(g, fast, contacted, berry_eaten);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player *p = &g->players[i];
        if (p->alive && p->pacman && !berry_eaten[i]) --p->pacman;
    }
    replenish(g);
    check_winner(g);
}
