#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#include "game_config.h"

#define PROTOCOL_MAGIC 0x534e4b32u
#define PROTOCOL_VERSION 2u
/* Fixed-size, network-byte-order frames. No native ints, pointers or socket IDs. */
typedef struct {
    uint32_t magic, version;
    char name[NAME_SIZE];
} Hello;
typedef struct {
    uint32_t active, alive, participating, color, length, score;
    uint32_t boost, pacman, respawn, head_x, head_y;
    char name[NAME_SIZE];
} PlayerView;
typedef struct {
    uint32_t magic, version, self, state, mode, host, winner;
    uint32_t paused, tps, countdown, tick, target;
    PlayerView players[MAX_PLAYERS];
    int32_t map[HEIGHT][WIDTH];
    char winner_name[NAME_SIZE];
} Frame;
_Static_assert(sizeof(Hello) == 8 + NAME_SIZE, "Unexpected Hello padding");
_Static_assert(sizeof(PlayerView) == 44 + NAME_SIZE, "Unexpected player padding");
_Static_assert(sizeof(Frame) == 48 + MAX_PLAYERS * sizeof(PlayerView) + HEIGHT * WIDTH * 4 + NAME_SIZE,
               "Unexpected frame padding");

void frame_encode(Frame *frame);
int frame_decode(Frame *frame);
int set_nonblocking(int fd);
long long monotonic_ms(void);
#endif
