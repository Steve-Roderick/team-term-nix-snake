#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

#define PORT            5500
#define HEIGHT          24
#define WIDTH           80
#define MAX_PLAYERS     8
#define WIN_LENGTH      15000      // Configurable Max Score: Effectively: Last worm standing.
                                   
#define TICK_RATE_US    200000   // 200ms = 20 FPS (100ms too fast, 200ms feels better).

// Countdown from 9 to 0 on game start.
// Two frames per count and the final frame is the first real in-game frame so:
// (10 + 1) * 2.
#define CNT_DOWN_FRAMES 22

// Initial length of each snake.
#define SNK_START_LEN 8

// Each fruit consumed adds x length to snake.
#define SNK_FRUIT_LEN 2

// Map Values
#define EMPTY           0
#define FRUIT           -111
#define WALL            -999
#define BORDER          -99

// Protocol States
#define STATE_WAITING   0
#define STATE_RUNNING   1
#define STATE_GAME_OVER 2

// Key Codes sent over socket
#define UP_KEY          'W'
#define DOWN_KEY        'S'
#define LEFT_KEY        'A'
#define RIGHT_KEY       'D'
#define START_KEY       'G'
#define QUIT_KEY        '.'

typedef enum {
    UP, DOWN, LEFT, RIGHT
} direction;

#endif
