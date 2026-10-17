#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include "game_config.h"

typedef struct { int x, y; } Point;
typedef struct {
    int active, alive, participating, color;
    char name[NAME_SIZE];
    int length, score, boost, pacman, respawn;
    Point body[MAX_LENGTH];
    direction dir, pending_dir;
} Player;

typedef struct {
    /* Terrain never contains snakes; rendering and collision occupancy are separate. */
    int terrain[HEIGHT][WIDTH];
    Player players[MAX_PLAYERS];
    int state, mode, host, winner, paused, tps, countdown, starters;
    char winner_name[NAME_SIZE];
    uint32_t tick;
} Game;

void game_init(Game *g);
int game_join(Game *g, const char *name);
void game_leave(Game *g, int id);
void game_input(Game *g, int id, unsigned char key);
void game_tick(Game *g);
void game_render(const Game *g, int map[HEIGHT][WIDTH]);
#endif
