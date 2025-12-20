#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <ncurses.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <signal.h>
#include "game_config.h"

// --- Global Client State ---
int sockfd;
int game_map[HEIGHT][WIDTH];
int current_state = STATE_WAITING;
int winner = 0;
int running = 1;

void finish_ncurses() {
    endwin();
    printf("Game Exited.\n");
    exit(0);
}

void handle_sigint(int sig) {
    running = 0;
    finish_ncurses();
}

void init_colors() {
    start_color();
    use_default_colors();
    init_pair(1, COLOR_WHITE, COLOR_RED);     // P1
    init_pair(2, COLOR_WHITE, COLOR_GREEN);   // P2
    init_pair(3, COLOR_WHITE, COLOR_YELLOW);  // P3
    init_pair(4, COLOR_WHITE, COLOR_BLUE);    // P4
    init_pair(5, COLOR_WHITE, COLOR_MAGENTA); // P5
    init_pair(6, COLOR_WHITE, COLOR_CYAN);    // P6
    init_pair(7, COLOR_BLACK, COLOR_WHITE);   // P7
    init_pair(8, COLOR_RED, COLOR_BLUE);      // P8
    init_pair(9, COLOR_RED, COLOR_BLACK);     // Fruit
    init_pair(10, COLOR_WHITE, COLOR_BLACK);  // Wall
    init_pair(11, COLOR_GREEN, COLOR_BLACK);  // Countdown
}

// --- Thread: Network Listener ---
void* server_listener(void* arg) {
    int header_size = sizeof(int) * 2;
    int map_size = sizeof(game_map);
    int total_size = header_size + map_size;
    char *buffer = malloc(total_size);

    while (running) {
        int bytes_read = 0;
        // Read fixed size packet
        while (bytes_read < total_size) {
            int r = read(sockfd, buffer + bytes_read, total_size - bytes_read);
            if (r <= 0) {
                running = 0;
                free(buffer);
                finish_ncurses();
                printf("Disconnected from server.\n");
                exit(0);
            }
            bytes_read += r;
        }

        int offset = 0;
        memcpy(&current_state, buffer + offset, sizeof(int)); offset += sizeof(int);
        memcpy(&winner, buffer + offset, sizeof(int)); offset += sizeof(int);
        memcpy(game_map, buffer + offset, map_size);

        // Force UI update
        // We do drawing here to sync with map updates
        erase();
        
        if (current_state == STATE_WAITING) {
            mvprintw(HEIGHT/2, WIDTH/2 - 10, "WAITING FOR HOST TO START");
            mvprintw(HEIGHT/2 + 1, WIDTH/2 - 10, "Press 'G' to Start (If Host)");
        } 
        else if (current_state == STATE_GAME_OVER) {
            mvprintw(HEIGHT/2, WIDTH/2 - 10, "GAME OVER!");
            if (winner == -1)
                mvprintw(HEIGHT/2 + 1, WIDTH/2 - 10, "IT WAS A DRAW.");
            else
                mvprintw(HEIGHT/2 + 1, WIDTH/2 - 10, "PLAYER %d WON!", winner);
            
            mvprintw(HEIGHT/2 + 3, WIDTH/2 - 10, "Host press 'G' to restart.");
        }
        else {
            // Draw Map
            for (int y = 0; y < HEIGHT; y++) {
                for (int x = 0; x < WIDTH; x++) {
                    int val = game_map[y][x];
                    if (val == BORDER) {
                        mvaddch(y, x, '#');
                    } else if (val == WALL) {
                        attron(COLOR_PAIR(10));
                        mvaddch(y, x, ACS_CKBOARD);
                        attroff(COLOR_PAIR(10));
                    } else if (val == FRUIT) {
                        attron(COLOR_PAIR(9));
                        mvaddch(y, x, 'O');
                        attroff(COLOR_PAIR(9));
                    } else if (val > 0 && val <= MAX_PLAYERS) {
                        // Snake Body in range [1, MAX_PLAYERS
                        attron(COLOR_PAIR(val));
                        mvaddch(y, x, ' '); // Color block
                        attroff(COLOR_PAIR(val));
                    // Countdown logic before the game starts.
                    } else if (val >= '0' && val <= '9') {
                        attron(COLOR_PAIR(11));
                        mvaddch(y, x, game_map[y][x]);
                        attroff(COLOR_PAIR(11));
                    }

                }
            }
            mvprintw(HEIGHT, 2, "Use WASD to Move. '.' to Quit.");
        }
        refresh();
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }

    struct sockaddr_in serv_addr;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return 1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return 1;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return 1;
    }

    // Name Prompt
    char name[32];
    printf("Enter Username: ");
    fgets(name, 32, stdin);
    name[strcspn(name, "\n")] = 0;
    write(sockfd, name, strlen(name));

    // Ncurses Setup
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    timeout(100); // Non-blocking read for keys
    keypad(stdscr, TRUE);
    init_colors();

    signal(SIGINT, handle_sigint);

    // Start Listener Thread
    pthread_t tid;
    pthread_create(&tid, NULL, server_listener, NULL);

    // Input Loop (Main Thread)
    while (running) {
        int ch = getch();
        if (ch != ERR) {
            char key = 0;
            ch = toupper(ch);
            if (ch == 'W') key = UP_KEY;
            else if (ch == 'S') key = DOWN_KEY;
            else if (ch == 'A') key = LEFT_KEY;
            else if (ch == 'D') key = RIGHT_KEY;
            else if (ch == 'G') key = START_KEY;
            else if (ch == '.') {
                running = 0;
                break;
            }

            if (key != 0) {
                write(sockfd, &key, 1);
            }
        }
    }

    close(sockfd);
    finish_ncurses();
    return 0;
}
