#define _POSIX_C_SOURCE 200809L
#include "protocol.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>

/* htonl/ntohl are identical byte permutations; apply to each field explicitly. */
static void transform(Frame *f) {
#define SWAP(field) f->field = htonl(f->field)
    SWAP(magic); SWAP(version); SWAP(self); SWAP(state); SWAP(mode);
    SWAP(host); SWAP(winner); SWAP(paused); SWAP(tps); SWAP(countdown); SWAP(tick); SWAP(target);
#undef SWAP
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        PlayerView *p = &f->players[i];
#define SWAP(field) p->field = htonl(p->field)
        SWAP(active); SWAP(alive); SWAP(participating); SWAP(color); SWAP(length); SWAP(score);
        SWAP(boost); SWAP(pacman); SWAP(respawn); SWAP(head_x); SWAP(head_y);
#undef SWAP
    }
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x) f->map[y][x] = (int32_t)htonl((uint32_t)f->map[y][x]);
}
void frame_encode(Frame *f) { transform(f); }
int frame_decode(Frame *f) {
    transform(f);
    f->winner_name[NAME_SIZE - 1] = '\0';
    for (int c = 0; f->winner_name[c]; ++c)
        if ((unsigned char)f->winner_name[c] < 32 || (unsigned char)f->winner_name[c] > 126)
            f->winner_name[c] = '?';
    if (f->magic != PROTOCOL_MAGIC || f->version != PROTOCOL_VERSION ||
        !f->self || f->self > MAX_PLAYERS || f->state > STATE_GAME_OVER ||
        f->mode > MODE_LAST_STANDING || f->host > MAX_PLAYERS || f->winner > MAX_PLAYERS ||
        f->tps < MIN_TPS || f->tps > MAX_TPS || f->paused > 1 ||
        f->countdown > COUNTDOWN_TICKS || !f->target || f->target > INT_MAX) return 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        PlayerView *p = &f->players[i];
        p->name[NAME_SIZE - 1] = '\0';
        for (int c = 0; p->name[c]; ++c)
            if ((unsigned char)p->name[c] < 32 || (unsigned char)p->name[c] > 126) p->name[c] = '?';
        if (p->active > 1 || p->alive > 1 || p->participating > 1 || p->color > COLOR_COUNT ||
            (p->active && !p->color) || p->length > MAX_LENGTH || p->score > INT_MAX ||
            p->boost > BOOST_TICKS || p->pacman > PACMAN_TICKS || p->respawn > RESPAWN_TICKS ||
            p->head_x >= WIDTH || p->head_y >= HEIGHT) return 0;
    }
    return f->players[f->self - 1].active != 0;
}
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
long long monotonic_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
