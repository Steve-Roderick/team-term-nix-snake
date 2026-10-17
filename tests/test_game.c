#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "protocol.h"

static Game g;
static void no_food(void) {
    for (int y = 1; y < HEIGHT - 1; ++y)
        for (int x = 1; x < WIDTH - 1; ++x)
            if (g.terrain[y][x] == FRUIT || g.terrain[y][x] == BERRY) g.terrain[y][x] = EMPTY;
}
static void tick(void) { game_tick(&g); no_food(); }
static void setup(int count) {
    game_init(&g);
    for (int i = 0; i < count; ++i) assert(game_join(&g, "Tester") == i);
    g.state = STATE_RUNNING; g.starters = count;
}
static void snake(int id, int x, int y, int length, direction d) {
    Player *p = &g.players[id];
    p->active = p->alive = p->participating = 1;
    p->length = length; p->dir = p->pending_dir = d;
    for (int b = 0; b < length; ++b) {
        p->body[b] = (Point){x, y};
        if (d == RIGHT) --x;
        if (d == LEFT) ++x;
        if (d == UP) ++y;
        if (d == DOWN) --y;
    }
}
static void collision_scene(void) {
    setup(2);
    snake(0, 10, 10, 8, RIGHT);
    snake(1, 11, 8, 8, UP);
}
static void assert_valid(void) {
    int map[HEIGHT][WIDTH] = {{0}};
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player *p = &g.players[i];
        assert(p->score >= 0 && p->boost >= 0 && p->boost <= BOOST_TICKS);
        assert(p->pacman >= 0 && p->pacman <= PACMAN_TICKS);
        if (!p->alive) continue;
        assert(p->active && p->length > 0 && p->length <= MAX_LENGTH);
        for (int b = 0; b < p->length; ++b) {
            Point pos = p->body[b];
            assert(pos.x > 0 && pos.x < WIDTH - 1 && pos.y > 0 && pos.y < HEIGHT - 1);
            assert(g.terrain[pos.y][pos.x] != WALL);
            assert(map[pos.y][pos.x] == 0 || map[pos.y][pos.x] == i + 1);
            map[pos.y][pos.x] = i + 1;
        }
    }
}
static void test_lobby(void) {
    game_init(&g);
    assert(game_join(&g, "Host") == 0);
    assert(game_join(&g, "Guest") == 1);
    game_input(&g, 1, '6'); assert(g.players[1].color == 6);
    game_input(&g, 1, MODE_KEY); assert(g.mode == MODE_MAX_SCORE);
    game_input(&g, 1, START_KEY); assert(g.state == STATE_WAITING);
    game_input(&g, 0, MODE_KEY); assert(g.mode == MODE_LAST_STANDING);
    game_leave(&g, 0); assert(g.host == 1);
    game_input(&g, 1, START_KEY); assert(g.state == STATE_RUNNING);
    assert(g.players[1].length == SNK_START_LEN && g.players[1].score == 0);
    assert(g.players[1].color == 6);
    Point head = g.players[1].body[0];
    for (int t = 0; t < COUNTDOWN_TICKS; ++t) tick();
    assert(g.players[1].body[0].x == head.x && g.players[1].body[0].y == head.y);
    assert(g.state == STATE_RUNNING); /* Solo survival doesn't instantly win. */
    game_leave(&g, 1); assert(g.state == STATE_WAITING && g.host == -1);
    for (int i = 0; i < MAX_PLAYERS; ++i) assert(game_join(&g, "Full") == i);
    assert(game_join(&g, "Overflow") == -1);
    game_input(&g, 0, START_KEY);
    assert_valid();
    for (int i = 0; i < MAX_PLAYERS; ++i) assert(g.players[i].length == SNK_START_LEN);
}
static void test_collision(void) {
    collision_scene(); tick();
    assert(g.players[0].alive && g.players[1].alive);
    assert(g.players[0].length == 4 && g.players[1].length == 12);
    assert(g.players[0].dir == LEFT && g.players[1].score == 4);
    assert(g.players[0].score == 0); assert_valid();
    g.terrain[10][6] = FRUIT; tick();
    assert(g.players[0].length == 6 && g.players[0].score == 2);
    assert(g.players[1].score == 4); /* Shrink and subsequent movement preserve accumulated score. */
    /* Same result with reversed player slots. */
    collision_scene();
    Player tmp = g.players[0]; g.players[0] = g.players[1]; g.players[1] = tmp;
    tick(); assert(g.players[1].length == 4 && g.players[0].length == 12);
    assert(g.players[0].score == 4); assert_valid();
    collision_scene(); snake(0, 10, 10, 7, RIGHT); tick();
    assert(g.players[0].length == 4 && g.players[1].length == 11);
    /* One segment cannot be halved further, so contact kills it. */
    collision_scene(); snake(0, 10, 10, 1, RIGHT); tick();
    assert(!g.players[0].alive && g.players[0].respawn == RESPAWN_TICKS);
    /* Simultaneous heads in the same empty cell halve both, without slot bias. */
    setup(2); snake(0, 10, 10, 8, RIGHT); snake(1, 12, 10, 8, LEFT); tick();
    assert(g.players[0].length == 4 && g.players[1].length == 4); assert_valid();
    /* Two simultaneous eaters share the same victim's half. */
    setup(3); snake(0, 10, 10, 8, RIGHT); snake(1, 12, 12, 8, LEFT);
    snake(2, 11, 8, 8, UP); g.players[0].pacman = g.players[1].pacman = 2;
    tick(); assert(g.players[2].length == 4);
    assert(g.players[0].length == 10 && g.players[1].length == 10);
    assert(g.players[0].score == 2 && g.players[1].score == 2); assert_valid();
    /* Crossing heads are also detected. */
    setup(2); snake(0, 10, 10, 8, RIGHT); snake(1, 11, 10, 8, LEFT); tick();
    assert(g.players[0].length == 4 && g.players[1].length == 4); assert_valid();
}
static void test_pacman(void) {
    collision_scene(); g.players[0].pacman = PACMAN_TICKS; tick();
    assert(g.players[0].length == 12 && g.players[0].score == 4);
    assert(g.players[1].length == 4 && g.players[1].dir == DOWN);
    assert(g.players[0].pacman == PACMAN_TICKS - 1); assert_valid();
    /* Pacman wins head-on against an unpowered snake. */
    setup(2); snake(0, 10, 10, 8, RIGHT); snake(1, 12, 10, 8, LEFT);
    g.players[0].pacman = 1; tick();
    assert(g.players[0].length == 12 && g.players[1].length == 4);
    assert(g.players[0].pacman == 0);
    /* Berry pickup and exact expiry. */
    setup(1); snake(0, 10, 10, 8, RIGHT); g.terrain[10][11] = BERRY; tick();
    assert(g.players[0].pacman == PACMAN_TICKS);
    for (int t = 0; t < PACMAN_TICKS; ++t) tick();
    assert(g.players[0].alive && g.players[0].pacman == 0 && g.players[0].body[0].x == 41);
    g.players[0].pacman = 1; g.terrain[10][42] = BERRY; tick();
    assert(g.players[0].pacman == PACMAN_TICKS);
}
static void test_boost(void) {
    setup(2); snake(0, 10, 10, 8, RIGHT); snake(1, 10, 15, 8, RIGHT);
    g.terrain[10][11] = FRUIT; tick();
    assert(g.players[0].body[0].x == 11 && g.players[0].boost == BOOST_TICKS);
    assert(g.players[0].length == 10 && g.players[0].score == 2);
    for (int t = 0; t < BOOST_TICKS; ++t) {
        int x = g.players[0].body[0].x, normal = g.players[1].body[0].x;
        tick(); assert(g.players[0].body[0].x == x + 2);
        assert(g.players[1].body[0].x == normal + 1);
    }
    assert(g.players[0].boost == 0); tick(); assert(g.players[0].body[0].x == 32);
    /* Refresh during a boosted tick, including eating on the second substep. */
    g.players[0].boost = 1; g.terrain[10][34] = FRUIT; tick();
    assert(g.players[0].boost == BOOST_TICKS && g.players[0].score == 4);
    /* Fast snakes still check intervening walls. */
    g.terrain[10][35] = WALL; tick(); assert(!g.players[0].alive);
    assert(g.players[0].boost == 0 && g.players[0].score == 3);
    /* The same pair cannot be bitten twice in one boosted tick. */
    collision_scene(); g.players[0].boost = 1; g.players[0].pacman = 2; tick();
    assert(g.players[0].length == 12 && g.players[0].score == 4 && g.players[1].length == 4);
}
static void test_pause_speed_input(void) {
    setup(2); snake(0, 10, 10, 8, RIGHT); snake(1, 30, 15, 8, RIGHT);
    g.players[0].boost = 8; g.players[0].pacman = 9;
    g.players[1].alive = 0; g.players[1].respawn = 4;
    game_input(&g, 1, PAUSE_KEY); /* Dead non-host can pause. */
    assert(g.paused); Game snapshot = g;
    for (int i = 0; i < 20; ++i) game_tick(&g);
    assert(memcmp(&g, &snapshot, sizeof(g)) == 0);
    for (int i = 0; i < 100; ++i) game_input(&g, 1, '+');
    assert(g.tps == MAX_TPS);
    for (int i = 0; i < 100; ++i) game_input(&g, 1, '-');
    assert(g.tps == MIN_TPS && g.paused);
    game_input(&g, 1, PAUSE_KEY); tick(); assert(g.tick == 1 && g.players[1].respawn == 3);
    /* Rapid input cannot reverse the direction actually used last tick. */
    setup(1); snake(0, 10, 10, 8, RIGHT);
    game_input(&g, 0, UP_KEY); game_input(&g, 0, LEFT_KEY); game_input(&g, 0, '+');
    assert(g.players[0].pending_dir == UP); tick();
    assert(g.players[0].body[0].y == 9);
}
static void test_death_respawn_modes(void) {
    setup(1); snake(0, 1, 10, 8, LEFT); g.players[0].score = 17; tick();
    assert(!g.players[0].alive && g.players[0].score == 12);
    assert(g.state == STATE_RUNNING && g.players[0].respawn == RESPAWN_TICKS);
    for (int t = 1; t < RESPAWN_TICKS; ++t) { tick(); assert(!g.players[0].alive); }
    tick(); assert(g.players[0].alive && g.players[0].length == SNK_START_LEN && g.players[0].score == 12);
    assert_valid();
    /* If the board is full, retry later without hanging or overwriting it. */
    setup(1); g.players[0].participating = 1; g.players[0].respawn = 1;
    for (int y = 1; y < HEIGHT - 1; ++y)
        for (int x = 1; x < WIDTH - 1; ++x) g.terrain[y][x] = WALL;
    tick(); assert(!g.players[0].alive && g.players[0].respawn == 1);
    for (int x = 1; x < WIDTH - 1; ++x) g.terrain[10][x] = EMPTY;
    tick(); assert(g.players[0].alive); assert_valid();
    setup(2); g.mode = MODE_LAST_STANDING;
    snake(0, 1, 10, 8, LEFT); snake(1, 30, 15, 8, RIGHT); tick();
    assert(g.state == STATE_GAME_OVER && g.winner == 1 && !g.players[0].respawn);
    for (int t = 0; t < 30; ++t) tick();
    assert(!g.players[0].alive);
    game_input(&g, 0, '3'); game_input(&g, 0, START_KEY);
    assert(g.state == STATE_RUNNING && g.players[0].alive && g.players[0].color == 3);
    /* An eliminated player stays dead even while other players keep the round alive. */
    setup(3); g.mode = MODE_LAST_STANDING;
    snake(0, 1, 10, 8, LEFT); snake(1, 10, 15, 8, RIGHT); snake(2, 10, 20, 8, RIGHT);
    for (int t = 0; t < RESPAWN_TICKS + 1; ++t) tick();
    assert(g.state == STATE_RUNNING && !g.players[0].alive && !g.players[0].respawn);
    setup(2); g.mode = MODE_LAST_STANDING;
    snake(0, 1, 10, 8, LEFT); snake(1, 78, 15, 8, RIGHT); tick();
    assert(g.state == STATE_GAME_OVER && g.winner == -1);
    setup(2); g.mode = MODE_LAST_STANDING;
    snake(0, 10, 10, 8, RIGHT); snake(1, 30, 15, 8, RIGHT);
    int late = game_join(&g, "Late"); assert(late == 2 && !g.players[late].participating);
    game_leave(&g, 0); assert(g.state == STATE_GAME_OVER && g.winner == 1 && g.host == 1);
    setup(1); snake(0, 10, 10, 8, RIGHT);
    late = game_join(&g, "Late"); assert(g.players[late].alive && g.players[late].participating);
    assert_valid();
    /* Max score uses accumulated length, not current body size. */
    setup(1); snake(0, 10, 10, 8, RIGHT); g.players[0].score = WIN_SCORE - 2;
    g.terrain[10][11] = FRUIT; tick();
    assert(g.state == STATE_GAME_OVER && g.winner == 0 && g.players[0].length == 10);
    /* The result retains the winner's identity even if their slot is reused. */
    game_join(&g, "Remaining");
    game_leave(&g, 0);
    assert(game_join(&g, "Replacement") == 0);
    assert(strcmp(g.winner_name, "Tester") == 0);
}
static void test_self_collision(void) {
    setup(1); snake(0, 10, 10, 4, RIGHT);
    g.players[0].body[1] = (Point){10, 11};
    g.players[0].body[2] = (Point){11, 11};
    g.players[0].body[3] = (Point){11, 10};
    tick(); assert(g.players[0].alive); /* Vacating own tail is safe. */
    setup(1); snake(0, 10, 10, 4, RIGHT);
    g.players[0].body[1] = (Point){11, 10}; tick(); assert(!g.players[0].alive);
}
static void test_capacity(void) {
    collision_scene();
    Player *p = &g.players[1];
    Point tail = p->body[p->length - 1];
    while (p->length < MAX_LENGTH) p->body[p->length++] = tail;
    tick(); assert(p->length == MAX_LENGTH && p->score == 4); assert_valid();
    collision_scene(); g.players[1].score = INT_MAX - 1; tick();
    assert(g.players[1].score == INT_MAX);
}
static void test_protocol(void) {
    Frame f = { .magic = PROTOCOL_MAGIC, .version = PROTOCOL_VERSION, .self = 1,
                .state = STATE_RUNNING, .tps = DEFAULT_TPS, .host = 1, .target = WIN_SCORE };
    f.players[0].active = 1; f.players[0].color = 3;
    strcpy(f.players[0].name, "Alice"); f.map[1][2] = BERRY; f.map[4][5] = 1;
    Frame original = f; frame_encode(&f);
    const unsigned char *bytes = (const unsigned char *)&f;
    assert(bytes[0] == 'S' && bytes[1] == 'N' && bytes[2] == 'K' && bytes[3] == '2');
    assert(frame_decode(&f) && memcmp(&f, &original, sizeof(f)) == 0);
    f.self = MAX_PLAYERS + 1; frame_encode(&f); assert(!frame_decode(&f));
    f = original; f.players[0].active = 0; f.players[0].color = UINT32_MAX;
    frame_encode(&f); assert(!frame_decode(&f));
}
static void test_random_games(void) {
    const unsigned char keys[] = "WASDPP+-";
    for (int round = 0; round < 100; ++round) {
        game_init(&g);
        for (int i = 0; i < MAX_PLAYERS; ++i) game_join(&g, "Stress");
        g.mode = round % 2;
        game_input(&g, 0, START_KEY);
        for (int t = 0; t < 1000; ++t) {
            game_input(&g, rand() % MAX_PLAYERS, keys[rand() % (sizeof(keys) - 1)]);
            game_tick(&g); assert_valid();
            if (g.state == STATE_GAME_OVER) game_input(&g, g.host, START_KEY);
        }
    }
}
int main(void) {
    srand(42);
    test_lobby(); test_collision(); test_pacman(); test_boost(); test_pause_speed_input();
    test_death_respawn_modes(); test_self_collision(); test_capacity(); test_protocol(); test_random_games();
    puts("Game rules, protocol and 100,000 simulated ticks passed.");
    return 0;
}
