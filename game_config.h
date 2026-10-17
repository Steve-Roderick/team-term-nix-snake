#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

#define PORT 5500
#define HEIGHT 24
#define WIDTH 80
#define MAX_PLAYERS 8
#define MAX_LENGTH ((HEIGHT - 2) * (WIDTH - 2))
#define NAME_SIZE 32
#define COLOR_COUNT 8
#define WIN_SCORE 150
#define DEFAULT_TPS 5
#define MIN_TPS 2
#define MAX_TPS 20
#define COUNTDOWN_TICKS 15
#define SNK_START_LEN 8
#define SNK_FRUIT_LEN 2
#define BOOST_TICKS 10
#define PACMAN_TICKS 30
#define RESPAWN_TICKS 10
#define APPLE_COUNT 5
#define BERRY_COUNT 2

#define EMPTY 0
#define FRUIT -111
#define BERRY -112
#define WALL -999
#define BORDER -99

#define STATE_WAITING 0
#define STATE_RUNNING 1
#define STATE_GAME_OVER 2
#define MODE_MAX_SCORE 0
#define MODE_LAST_STANDING 1

#define UP_KEY 'W'
#define DOWN_KEY 'S'
#define LEFT_KEY 'A'
#define RIGHT_KEY 'D'
#define START_KEY 'G'
#define QUIT_KEY '.'
#define PAUSE_KEY 'P'
#define MODE_KEY 'M'

typedef enum { UP, DOWN, LEFT, RIGHT } direction;
#endif
