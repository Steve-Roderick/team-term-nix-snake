#ifndef GAME_VIEW_H
#define GAME_VIEW_H
#include "game.h"
#include "protocol.h"

/* Shared rendering snapshot; local play does not encode or send it. */
void game_snapshot(const Game *g, int self, Frame *f);
#endif
