#include <assert.h>
#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include "local.h"
#include "game_view.h"

int main(void) {
    Game g;
    local_init(&g);
    assert(g.players[0].active && g.players[1].active && !g.players[2].active);
    assert(g.players[0].color == 1 && g.players[1].color == 6);
    local_input(&g, 'a'); assert(g.players[0].color == 8 && g.players[1].color == 6);
    local_input(&g, 'D'); assert(g.players[0].color == 1);
    local_input(&g, KEY_RIGHT); assert(g.players[1].color == 7 && g.players[0].color == 1);
    local_input(&g, KEY_LEFT); assert(g.players[1].color == 6);
    local_input(&g, KEY_UP); assert(g.players[1].color == 6);
    local_input(&g, 'm'); assert(g.mode == MODE_LAST_STANDING);
    local_input(&g, 'g'); assert(g.state == STATE_RUNNING && g.players[0].alive && g.players[1].alive);
    const int arrows[] = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT};
    const int wasd[] = {'w', 's', 'a', 'd'};
    const direction expected[] = {UP, DOWN, LEFT, RIGHT};
    for (int i = 0; i < 4; ++i) {
        direction initial = i < 2 ? RIGHT : UP;
        g.players[0].dir = g.players[0].pending_dir = initial;
        g.players[1].dir = g.players[1].pending_dir = initial;
        local_input(&g, wasd[i]);
        assert(g.players[0].pending_dir == expected[i] && g.players[1].pending_dir == initial);
        local_input(&g, arrows[i]);
        assert(g.players[1].pending_dir == expected[i] && g.players[0].pending_dir == expected[i]);
    }
    /* Both pending turns take effect in the same simulation tick. */
    g.countdown = 0;
    for (int i = 0; i < 2; ++i) {
        Player *p = &g.players[i];
        p->length = 3; p->dir = p->pending_dir = RIGHT;
        for (int b = 0; b < 3; ++b) p->body[b] = (Point){20 + i * 20 - b, 10};
    }
    memset(g.terrain, 0, sizeof(g.terrain));
    local_input(&g, 'w'); local_input(&g, KEY_DOWN); game_tick(&g);
    assert(g.players[0].body[0].y == 9 && g.players[1].body[0].y == 11);
    local_input(&g, 'p'); Game frozen = g;
    game_tick(&g); assert(memcmp(&g, &frozen, sizeof(g)) == 0);
    local_input(&g, '+'); assert(g.tps == DEFAULT_TPS + 1);
    local_input(&g, '-'); assert(g.tps == DEFAULT_TPS);
    local_input(&g, 'p'); assert(!g.paused);
    Frame f;
    game_snapshot(&g, 0, &f);
    assert(f.players[0].head_y == 9 && f.players[1].head_y == 11);
    assert(f.map[9][20] == 1 && f.map[11][40] == 2 && f.self == 1);
    frame_encode(&f); assert(frame_decode(&f));
    /* A completed local round can switch mode and restart both players. */
    g.state = STATE_GAME_OVER; g.players[0].alive = 0;
    local_input(&g, 'm'); local_input(&g, 'g');
    assert(g.mode == MODE_MAX_SCORE && g.players[0].alive && g.players[1].alive);
    assert(local_input(&g, '.') == 0);
    puts("Local controls, independent movement, colours, shared settings and restart passed.");
    return 0;
}
