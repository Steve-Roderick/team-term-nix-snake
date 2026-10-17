#ifndef LOCAL_H
#define LOCAL_H
#include "game.h"

#define LOCAL_MIN_ROWS (HEIGHT + 7)
void local_init(Game *g);
/* ncurses key code in; nonzero means continue, zero means quit. */
int local_input(Game *g, int ch);
#endif
