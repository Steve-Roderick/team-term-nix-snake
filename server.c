#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include "game_config.h"

// --- Structures ---
typedef struct {
    int x, y;
} point;

typedef struct {
    int id;                 // Socket FD
    int active;
    char name[32];
    int color_id;           // 1-8
    
    // Snake State
    int length;
    point body[HEIGHT * WIDTH]; // Simple array for body parts
    direction dir;
    direction pending_dir;  // Input buffer to prevent 180 turns in one tick
    int alive;
} Player;

// --- Global State ---
int             game_map[HEIGHT][WIDTH];
Player          players[MAX_PLAYERS];
int             game_state = STATE_WAITING;
int             winner_id = 0;
pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

// --- Helper Functions ---

void init_map() {
    memset(game_map, 0, sizeof(game_map));
    for (int i = 0; i < HEIGHT; i++) {
        game_map[i][0] = game_map[i][WIDTH - 1] = BORDER;
    }
    for (int i = 0; i < WIDTH; i++) {
        game_map[0][i] = game_map[HEIGHT - 1][i] = BORDER;
    }
}

void spawn_fruit() {
    int x, y, attempts = 0;
    do {
        y = rand() % (HEIGHT - 2) + 1;
        x = rand() % (WIDTH - 2) + 1;
        attempts++;
    } while (game_map[y][x] != 0 && attempts < 100);
    
    if (game_map[y][x] == 0) game_map[y][x] = FRUIT;
}

void reset_game() {
    init_map();
    winner_id = 0;
    // Spawn random obstacles
    for(int i=0; i<5; i++) {
        int r_y = rand() % (HEIGHT - 4) + 2;
        int r_x = rand() % (WIDTH - 4) + 2;
        game_map[r_y][r_x] = WALL;
        game_map[r_y][r_x+1] = WALL; // Small wall segments
    }
    // Spawn Fruits
    for(int i=0; i<5; i++) spawn_fruit();

    // Reset snakes
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].active) {
            players[i].alive = 1;
            players[i].length = SNK_START_LEN;
            players[i].dir = UP;
            players[i].pending_dir = UP;
            
            // Find spawn spot
            int sy, sx, safe;
            do {
                safe = 1;
                sy = rand() % (HEIGHT - 6) + 3;
                sx = rand() % (WIDTH - 6) + 3;
                if(game_map[sy][sx] != 0) safe = 0;
            } while(!safe);

            players[i].body[0].y = sy;
            players[i].body[0].x = sx;
            players[i].body[1].y = sy + 1;
            players[i].body[1].x = sx;
            players[i].body[2].y = sy + 2;
            players[i].body[2].x = sx;
        }
    }
}

// --- Thread: Physics Engine ---
void* physics_loop(void* arg) {
    while (1) {
        usleep(TICK_RATE_US);
        
        pthread_mutex_lock(&state_mutex);
        
        if (game_state == STATE_RUNNING) {
            // 1. Clear old snake positions from map
            // We rebuild the map dynamic parts every tick to avoid ghosting
            for(int y=0; y<HEIGHT; y++) {
                for(int x=0; x<WIDTH; x++) {
                    if (game_map[y][x] > 0) game_map[y][x] = 0; // Clear snake bodies
                }
            }

            // 2. Move Snakes
            int active_count = 0;
            int alive_count = 0;

            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (!players[i].active) continue;
                active_count++;
                if (!players[i].alive) continue;
                alive_count++;

                Player *p = &players[i];
                p->dir = p->pending_dir; // Apply buffered input

                // Calculate new head
                point new_head = p->body[0];
                switch (p->dir) {
                    case UP:    new_head.y--; break;
                    case DOWN:  new_head.y++; break;
                    case LEFT:  new_head.x--; break;
                    case RIGHT: new_head.x++; break;
                }

                // Check collision (Wall, Border, Self, Others)
                int collision = 0;
                // Bounds check
                if (new_head.y <= 0 || new_head.y >= HEIGHT-1 || new_head.x <= 0 || new_head.x >= WIDTH-1) collision = 1;
                else if (game_map[new_head.y][new_head.x] == WALL) collision = 1;
                
                // Check against other snakes (using previous positions) is tricky. 
                // Simplified: Check against the map "state" isn't fully reliable here because we cleared it.
                // We must check coordinate against all other snake bodies.
                for (int j=0; j<MAX_PLAYERS; j++) {
                    if (!players[j].active || !players[j].alive) continue;
                    for (int b=0; b < players[j].length; b++) {
                        if (new_head.x == players[j].body[b].x && new_head.y == players[j].body[b].y) {
                            collision = 1;
                        }
                    }
                }

                if (collision) {
                    p->alive = 0; // Snake dies
                } else {
                    // Check Fruit
                    if (game_map[new_head.y][new_head.x] == FRUIT) {
                        p->length = p->length + SNK_FRUIT_LEN;
                        spawn_fruit();
                    }

                    // Move Body
                    for (int b = p->length - 1; b > 0; b--) {
                        p->body[b] = p->body[b - 1];
                    }
                    p->body[0] = new_head;

                    // Win Condition
                    if (p->length >= WIN_LENGTH) {
                        game_state = STATE_GAME_OVER;
                        winner_id = p->id;
                    }
                }
            }

            // 3. Re-draw valid snakes onto map
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (players[i].active && players[i].alive) {
                    for (int b = 0; b < players[i].length; b++) {
                        int y = players[i].body[b].y;
                        int x = players[i].body[b].x;
                        // Head is positive ID, body is same ID
                        game_map[y][x] = players[i].color_id; 
                    }
                }
            }
            
            // End Game if everyone died
            if (active_count > 0 && alive_count == 0) {
                game_state = STATE_GAME_OVER;
                winner_id = -1; // Draw
            }
        }

        // 4. Broadcast Map + Metadata
        // Format: [State(1)][Winner(4)][Map(size)]
        
        int buffer_size = sizeof(int) + sizeof(int) + sizeof(game_map);
        char *send_buffer = malloc(buffer_size);
        int offset = 0;
        
        memcpy(send_buffer + offset, &game_state, sizeof(int)); offset += sizeof(int);
        memcpy(send_buffer + offset, &winner_id, sizeof(int)); offset += sizeof(int);
        memcpy(send_buffer + offset, game_map, sizeof(game_map));

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (players[i].active) {
                if (send(players[i].id, send_buffer, buffer_size, MSG_NOSIGNAL) < 0) {
                    // Client disconnected during write
                    printf("Client %d disconnected during broadcast.\n", players[i].id);
                    close(players[i].id);
                    players[i].active = 0;
                }
            }
        }
        free(send_buffer);

        pthread_mutex_unlock(&state_mutex);
    }
    return NULL;
}

// --- Thread: Client Input Handler ---
void* client_handler(void* arg) {
    int fd = *(int*)arg;
    free(arg);
    
    char name_buffer[32];
    int n = read(fd, name_buffer, 32);
    if (n <= 0) { close(fd); return NULL; }
    name_buffer[n] = '\0'; // Ensure null term (might cut off last char if full)

    pthread_mutex_lock(&state_mutex);
    int p_index = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!players[i].active) {
            p_index = i;
            players[i].active = 1;
            players[i].id = fd;
            players[i].color_id = i + 1; // 1-based color
            memcpy(players[i].name, name_buffer, 32);
            players[i].name[31] = '\0';
            players[i].alive = 0; // Not alive until game starts
            printf("Player joined: %s (ID: %d)\n", players[i].name, fd);
            break;
        }
    }
    pthread_mutex_unlock(&state_mutex);

    if (p_index == -1) {
        write(fd, "FULL", 4);
        close(fd);
        return NULL;
    }

    // Input Loop
    char key;
    while (read(fd, &key, 1) > 0) {
        pthread_mutex_lock(&state_mutex);
        Player *p = &players[p_index];
        
        if (game_state == STATE_WAITING) {
            if (key == START_KEY && p_index == 0) { // First player is host
                printf("Game Started by %s\n", p->name);
                reset_game();
                game_state = STATE_RUNNING;
            }
        } else if (game_state == STATE_RUNNING && p->alive) {
            // Prevent 180 degree turns
            switch(key) {
                case UP_KEY:    if(p->dir != DOWN) p->pending_dir = UP; break;
                case DOWN_KEY:  if(p->dir != UP)   p->pending_dir = DOWN; break;
                case LEFT_KEY:  if(p->dir != RIGHT) p->pending_dir = LEFT; break;
                case RIGHT_KEY: if(p->dir != LEFT)  p->pending_dir = RIGHT; break;
            }
        } else if (game_state == STATE_GAME_OVER) {
             if (key == START_KEY && p_index == 0) {
                 game_state = STATE_WAITING; // Reset to lobby
             }
        }
        pthread_mutex_unlock(&state_mutex);
    }

    // Cleanup
    pthread_mutex_lock(&state_mutex);
    printf("Player disconnected: %s\n", players[p_index].name);
    players[p_index].active = 0;
    close(fd);
    
    // If host leaves, reset game state or assign new host?
    // For simplicity, if player count is 0, reset state
    int remaining = 0;
    for(int i=0; i<MAX_PLAYERS; i++) if(players[i].active) remaining++;
    if(remaining == 0) game_state = STATE_WAITING;
    
    pthread_mutex_unlock(&state_mutex);
    return NULL;
}

int main() {
    signal(SIGPIPE, SIG_IGN); // Ignore broken pipes
    srand(time(NULL));
    init_map();

    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Allow port reuse
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_PLAYERS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server started on port %d. Max Score: %d\n", PORT, WIN_LENGTH);

    // Start Physics Thread
    pthread_t physics_tid;
    pthread_create(&physics_tid, NULL, physics_loop, NULL);

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        int *pclient = malloc(sizeof(int));
        *pclient = new_socket;
        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, pclient);
        pthread_detach(tid);
    }

    return 0;
}
