#include "local.h"
#include <ctype.h>
#include <ncurses.h>

void local_init(Game *g) {
    game_init(g);
    game_join(g, "P1 WASD");
    game_join(g, "P2 arrows");
    game_input(g, 1, '6');
}
int local_input(Game *g, int ch) {
    int player = 0;
    unsigned char key = 0;
    switch (ch) {
        case KEY_UP: player = 1; key = UP_KEY; break;
        case KEY_DOWN: player = 1; key = DOWN_KEY; break;
        case KEY_LEFT: player = 1; key = LEFT_KEY; break;
        case KEY_RIGHT: player = 1; key = RIGHT_KEY; break;
        default:
            if (ch >= 0 && ch <= 255) key = (unsigned char)toupper((unsigned char)ch);
            break;
    }
    if (key == QUIT_KEY) return 0;
    if (g->state != STATE_RUNNING && (key == LEFT_KEY || key == RIGHT_KEY)) {
        int color = g->players[player].color - 1;
        color = (color + (key == RIGHT_KEY ? 1 : COLOR_COUNT - 1)) % COLOR_COUNT;
        game_input(g, player, (unsigned char)('1' + color));
    } else if (key) game_input(g, player, key);
    return 1;
}
