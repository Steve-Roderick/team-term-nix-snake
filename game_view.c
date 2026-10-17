#include "game_view.h"
#include <string.h>

void game_snapshot(const Game *g, int self, Frame *f) {
    memset(f, 0, sizeof(*f));
    f->magic = PROTOCOL_MAGIC; f->version = PROTOCOL_VERSION;
    f->self = (uint32_t)self + 1; f->state = (uint32_t)g->state;
    f->mode = (uint32_t)g->mode; f->host = (uint32_t)(g->host + 1);
    f->winner = (uint32_t)(g->winner + 1); f->paused = (uint32_t)g->paused;
    f->tps = (uint32_t)g->tps; f->countdown = (uint32_t)g->countdown;
    f->tick = g->tick; f->target = WIN_SCORE;
    memcpy(f->winner_name, g->winner_name, NAME_SIZE);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        const Player *p = &g->players[i];
        PlayerView *v = &f->players[i];
        v->active = (uint32_t)p->active; v->alive = (uint32_t)p->alive;
        v->participating = (uint32_t)p->participating; v->color = (uint32_t)p->color;
        v->length = (uint32_t)p->length; v->score = (uint32_t)p->score;
        v->boost = (uint32_t)p->boost; v->pacman = (uint32_t)p->pacman;
        v->respawn = (uint32_t)p->respawn;
        if (p->alive) { v->head_x = (uint32_t)p->body[0].x; v->head_y = (uint32_t)p->body[0].y; }
        memcpy(v->name, p->name, NAME_SIZE);
    }
    int map[HEIGHT][WIDTH];
    game_render(g, map);
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x) f->map[y][x] = map[y][x];
}
