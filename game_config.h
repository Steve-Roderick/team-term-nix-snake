#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

#define PORT            5500
#define HEIGHT          24
#define WIDTH           80
#define MAX_PLAYERS     8
#define WIN_LENGTH      15      // Configurable Max Score
#define TICK_RATE_US    100000  // 100ms = 10 FPS

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
