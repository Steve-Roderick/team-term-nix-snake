#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <ncurses.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "protocol.h"
#include "game_view.h"
#include "local.h"

static volatile sig_atomic_t running = 1;
static int colors;
static int local_mode;
static const char *color_names[] = { "", "Red", "Green", "Yellow", "Blue", "Magenta", "Cyan", "White", "Grey" };
static void stop(int sig) { (void)sig; running = 0; }
/* Clip text instead of allowing curses to wrap HUD lines over the next row. */
static void line(int row, int col, const char *format, ...) {
    if (row < 0 || row >= LINES || col < 0 || col >= COLS - 1) return;
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    mvaddnstr(row, col, text, COLS - col - 1);
}
static void append(const char *format, ...) {
    int row, col;
    getyx(stdscr, row, col);
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    line(row, col, "%s", text);
}
static void init_colors(void) {
    colors = has_colors();
    if (!colors) return;
    start_color();
    const short palette[] = { COLOR_RED, COLOR_GREEN, COLOR_YELLOW, COLOR_BLUE,
                              COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE, COLOR_BLACK };
    for (short i = 0; i < COLOR_COUNT; ++i)
        init_pair((short)(i + 1), i == 7 ? COLOR_WHITE : COLOR_BLACK, palette[i]);
    init_pair(9, COLOR_RED, COLOR_BLACK);
    init_pair(10, COLOR_YELLOW, COLOR_BLACK);
    init_pair(11, COLOR_YELLOW, COLOR_MAGENTA); /* Distinct from every body palette. */
}
static void player_line(const Frame *f, int i, int row) {
    const PlayerView *p = &f->players[i];
    if (colors) attron(COLOR_PAIR(p->color));
    line(row, 0, " %d ", i + 1);
    if (colors) attroff(COLOR_PAIR(p->color));
    const char *status = f->state != STATE_RUNNING ? "ready" :
        p->alive ? "alive" : p->respawn ? "respawning" : "spectating";
    if (local_mode) {
        line(row, 4, "%s len:%u score:%u boost:%u Pacman:%u respawn:%u %s",
             p->name, p->length, p->score, p->boost, p->pacman, p->respawn, status);
        return;
    }
    line(row, 4, "%-15.15s %s %s len:%-4u score:%-5u %s",
             p->name, f->self == (uint32_t)i + 1 ? "YOU " : "    ",
             f->host == (uint32_t)i + 1 ? "HOST" : "    ", p->length, p->score, status);
}
static void draw(const Frame *f, int received) {
    erase();
    if (local_mode && (COLS < WIDTH || LINES < LOCAL_MIN_ROWS)) {
        line(0, 0, "Local play: resize to at least %d columns x %d rows.", WIDTH, LOCAL_MIN_ROWS);
        line(2, 0, "Both players need to see the full arena.");
        line(3, 0, "Game paused. Resize, then P to resume. . to quit.");
        refresh(); return;
    }
    if (COLS < 60 || LINES < 20) {
        line(0, 0, "Resize terminal to at least 60 columns x 20 rows.");
        line(2, 0, "P: pause/resume  +/-: speed  .: quit");
        refresh(); return;
    }
    if (!received) { line(2, 2, "Connecting to lobby..."); refresh(); return; }
    const PlayerView *self = &f->players[f->self - 1];
    const char *mode = f->mode == MODE_MAX_SCORE ? "Max score" : "Last worm standing";
    if (f->state != STATE_RUNNING) {
        line(0, 2, "MULTIPLAYER SNAKE - %s", f->state == STATE_GAME_OVER ? "ROUND OVER" : "LOBBY");
        if (f->state == STATE_GAME_OVER) {
            if (f->winner) line(1, 2, "%s (Player %u) won!", f->winner_name, f->winner);
            else line(1, 2, "Draw - no single winner.");
        }
        line(3, 2, "Mode: %s", mode);
        if (f->mode == MODE_MAX_SCORE) append(" (target: %u)", f->target);
        if (local_mode) {
            line(4, 2, "LOCAL TWO PLAYER | P1: WASD | P2: arrows | %u ticks/sec", f->tps);
            line(6, 2, "P1 A/D colour: %s | P2 Left/Right colour: %s",
                 color_names[f->players[0].color], color_names[f->players[1].color]);
        } else {
            line(4, 2, "Speed: %u ticks/sec | You: Player %u | Host: Player %u", f->tps, f->self, f->host);
            line(6, 2, "Choose your colour with 1-8 (now %s):", color_names[self->color]);
        }
        for (int i = 1; i <= COLOR_COUNT; ++i) {
            int row = 7 + (i - 1) / 4, col = 2 + ((i - 1) % 4) * 14;
            if (colors) attron(COLOR_PAIR(i));
            line(row, col, "%d %-8s", i, color_names[i]);
            if (colors) attroff(COLOR_PAIR(i));
        }
        int row = 10;
        for (int i = 0; i < MAX_PLAYERS; ++i) if (f->players[i].active) player_line(f, i, row++);
        line(LINES - 2, 0, "%sG start/restart | M change mode", local_mode ? "" : "Host: ");
        line(LINES - 1, 0, local_mode ? "P1 A/D colour | P2 Left/Right colour | +/- speed | . quit" :
             "Everyone: 1-8 colour | +/- speed | . quit");
    } else {
        int count = 0;
        for (int i = 0; i < MAX_PLAYERS; ++i) count += f->players[i].active != 0;
        int view_h = LINES - count - 5, view_w = COLS < WIDTH ? COLS : WIDTH;
        if (view_h > HEIGHT) view_h = HEIGHT;
        int cx = self->alive ? (int)self->head_x : WIDTH / 2;
        int cy = self->alive ? (int)self->head_y : HEIGHT / 2;
        int ox = cx - view_w / 2, oy = cy - view_h / 2;
        if (ox < 0) ox = 0;
        if (oy < 0) oy = 0;
        if (ox + view_w > WIDTH) ox = WIDTH - view_w;
        if (oy + view_h > HEIGHT) oy = HEIGHT - view_h;
        line(0, 0, "%s | %u ticks/s | %s", mode, f->tps, f->paused ? "PAUSED" : "playing");
        if (f->countdown) append(" | Start in %u ticks", f->countdown);
        for (int y = 0; y < view_h; ++y) for (int x = 0; x < view_w; ++x) {
            int value = f->map[y + oy][x + ox], pair = 0;
            chtype glyph = ' ';
            if (value == BORDER) glyph = ':';
            else if (value == FRUIT) { glyph = 'O'; pair = 9; }
            else if (value == BERRY) { glyph = '*'; pair = 10; }
            else if (value > 0 && value <= MAX_PLAYERS) {
                const PlayerView *p = &f->players[value - 1];
                pair = (int)p->color;
                glyph = (chtype)('0' + value);
                if (p->head_x == (uint32_t)(x + ox) && p->head_y == (uint32_t)(y + oy)) {
                    glyph = p->pacman ? 'C' : '@';
                    if (p->pacman) { pair = 11; glyph |= A_BOLD; }
                }
            }
            if (colors && pair) attron(COLOR_PAIR(pair));
            mvaddch(y + 1, x, glyph);
            if (colors && pair) attroff(COLOR_PAIR(pair));
        }
        int row = view_h + 1;
        if (local_mode) line(row++, 0, "LOCAL TWO PLAYER | P1: WASD | P2: arrows | Target: %u", f->target);
        else line(row++, 0, "YOU #%u len:%u score:%u/%u boost:%u Pacman:%u respawn:%u",
                  f->self, self->length, self->score, f->target, self->boost, self->pacman, self->respawn);
        line(row++, 0, local_mode ? "P pause/resume | +/- speed | . quit" :
             "WASD/arrows move | P pause/resume | +/- speed | . quit");
        for (int i = 0; i < MAX_PLAYERS; ++i) if (f->players[i].active) player_line(f, i, row++);
        line(row, 0, "Edges wrap | O: 2x/10 ticks | *: Pacman/30 ticks");
        if (view_h < HEIGHT || view_w < WIDTH) append(" | scrolling view");
    }
    refresh();
}
static int play_local(void) {
    Game game;
    Frame view;
    local_mode = 1;
    srand((unsigned int)time(NULL));
    local_init(&game);
    signal(SIGINT, stop); signal(SIGTERM, stop);
    initscr(); cbreak(); noecho(); curs_set(0); keypad(stdscr, TRUE); timeout(0); init_colors();
    long long next_tick = monotonic_ms() + 1000 / game.tps;
    long long next_draw = 0;
    while (running) {
        int dirty = 0, ch;
        /* Bound input work so held keys cannot starve game ticks. */
        for (int n = 0; n < 64 && (ch = getch()) != ERR; ++n) {
            if (!local_input(&game, ch)) { running = 0; break; }
            dirty = 1;
        }
        if (!running) break;
        if (game.state == STATE_RUNNING && (COLS < WIDTH || LINES < LOCAL_MIN_ROWS)) game.paused = 1;
        long long now = monotonic_ms();
        if (now >= next_tick) {
            game_tick(&game);
            next_tick = now + 1000 / game.tps;
            dirty = 1;
        }
        if (dirty || now >= next_draw) {
            game_snapshot(&game, 0, &view);
            draw(&view, 1);
            next_draw = now + 50;
        }
        napms(10);
    }
    endwin();
    puts("Local game exited.");
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--local") == 0) return play_local();
    if (argc < 2 || argc > 3 || strcmp(argv[1], "--local") == 0) {
        fprintf(stderr, "Usage: %s --local | <server_ipv4> [port]\n", argv[0]); return 1;
    }
    long port = PORT;
    if (argc == 3) {
        char *end;
        port = strtol(argv[2], &end, 10);
        if (!argv[2][0] || *end || port < 1 || port > 65535) { fprintf(stderr, "Invalid port\n"); return 1; }
    }
    Hello hello = { .magic = htonl(PROTOCOL_MAGIC), .version = htonl(PROTOCOL_VERSION) };
    printf("Enter Username: "); fflush(stdout);
    if (!fgets(hello.name, sizeof(hello.name), stdin)) return 1;
    size_t name_len = strcspn(hello.name, "\n");
    if (hello.name[name_len] == '\n') hello.name[name_len] = '\0';
    else { int c; while ((c = getchar()) != '\n' && c != EOF) {} }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
    if (inet_pton(AF_INET, argv[1], &address.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address\n"); close(fd); return 1;
    }
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0 || set_nonblocking(fd) < 0) {
        perror("connect"); close(fd); return 1;
    }
    signal(SIGINT, stop); signal(SIGTERM, stop); signal(SIGPIPE, SIG_IGN);
    initscr(); cbreak(); noecho(); curs_set(0); keypad(stdscr, TRUE); timeout(0); init_colors();
    unsigned char output[512];
    memcpy(output, &hello, sizeof(hello));
    size_t output_len = sizeof(hello), used = 0;
    Frame incoming, current = {0};
    int received = 0, failed = 0;
    const char *message = "Game exited.";
    draw(&current, received);
    while (running) {
        int ch;
        while ((ch = getch()) != ERR) {
            unsigned char key = 0;
            if (ch == KEY_UP) key = UP_KEY;
            else if (ch == KEY_DOWN) key = DOWN_KEY;
            else if (ch == KEY_LEFT) key = LEFT_KEY;
            else if (ch == KEY_RIGHT) key = RIGHT_KEY;
            else if (ch >= 0 && ch <= 255) key = (unsigned char)toupper((unsigned char)ch);
            if (key == QUIT_KEY) { running = 0; break; }
            if (key && output_len < sizeof(output)) output[output_len++] = key;
            if (ch == KEY_RESIZE) draw(&current, received);
        }
        if (!running) break;
        struct pollfd event = { .fd = fd, .events = POLLIN | (output_len ? POLLOUT : 0) };
        int ready = poll(&event, 1, 30);
        if (ready < 0) { if (errno == EINTR) continue; failed = 1; message = "Network poll failed."; break; }
        if (event.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            failed = 1; message = "Disconnected (server closed, full, or incompatible version)."; break;
        }
        if (event.revents & POLLOUT) {
            ssize_t n = send(fd, output, output_len, MSG_NOSIGNAL);
            if (n > 0) { output_len -= (size_t)n; memmove(output, output + n, output_len); }
            else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                failed = 1; message = "Connection lost while sending input."; break;
            }
        }
        if (event.revents & POLLIN) {
            ssize_t n = recv(fd, (char *)&incoming + used, sizeof(incoming) - used, 0);
            if (n > 0) {
                used += (size_t)n;
                if (used == sizeof(incoming)) {
                    if (!frame_decode(&incoming)) { failed = 1; message = "Invalid or incompatible server frame."; break; }
                    current = incoming; received = 1; used = 0;
                    draw(&current, received);
                }
            } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                failed = 1; message = "Disconnected from server."; break;
            }
        }
    }
    shutdown(fd, SHUT_RDWR); close(fd);
    endwin();
    puts(message);
    return failed;
}
