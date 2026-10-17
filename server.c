#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "game_view.h"
#include "protocol.h"

#define MAX_CONNECTIONS (MAX_PLAYERS * 2)
#define CONNECTION_TIMEOUT_MS 5000
#define BROADCAST_MS 50

typedef struct {
    int fd, player;
    Hello hello;
    size_t hello_used, sent, queued;
    Frame output;
    long long connected_at, queued_at;
} Connection;
static Game game;
static Connection clients[MAX_CONNECTIONS];
static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }

static void disconnect_client(Connection *c) {
    if (c->fd < 0) return;
    if (c->player >= 0) game_leave(&game, c->player);
    close(c->fd);
    memset(c, 0, sizeof(*c));
    c->fd = c->player = -1;
}
static void queue_frame(Connection *c, long long now) {
    if (c->player < 0 || c->queued) return;
    Frame *f = &c->output;
    game_snapshot(&game, c->player, f);
    frame_encode(f);
    c->sent = 0; c->queued = sizeof(*f); c->queued_at = now;
}
static void receive_input(Connection *c) {
    unsigned char buffer[256];
    /* Bound work per poll iteration, so input flooding cannot starve physics. */
    ssize_t n = recv(c->fd, buffer, sizeof(buffer), 0);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        disconnect_client(c); return;
    }
    for (ssize_t i = 0; i < n; ++i) {
        if (c->player < 0) {
            ((unsigned char *)&c->hello)[c->hello_used++] = buffer[i];
            if (c->hello_used != sizeof(c->hello)) continue;
            if (ntohl(c->hello.magic) != PROTOCOL_MAGIC || ntohl(c->hello.version) != PROTOCOL_VERSION) {
                disconnect_client(c); return;
            }
            c->hello.name[NAME_SIZE - 1] = '\0';
            for (int j = 0; c->hello.name[j]; ++j)
                if ((unsigned char)c->hello.name[j] < 32 || (unsigned char)c->hello.name[j] > 126)
                    c->hello.name[j] = '?';
            c->player = game_join(&game, c->hello.name);
            if (c->player < 0) { disconnect_client(c); return; }
            printf("Player %d joined: %s\n", c->player + 1, game.players[c->player].name);
            queue_frame(c, monotonic_ms());
        } else {
            if (buffer[i] == QUIT_KEY) { disconnect_client(c); return; }
            game_input(&game, c->player, buffer[i]);
        }
    }
}
static void send_output(Connection *c) {
    if (!c->queued) return;
    ssize_t n = send(c->fd, (char *)&c->output + c->sent, c->queued - c->sent, MSG_NOSIGNAL);
    if (n > 0) {
        c->sent += (size_t)n;
        if (c->sent == c->queued) c->queued = c->sent = 0;
    } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) disconnect_client(c);
}
int main(int argc, char **argv) {
    long port = PORT;
    if (argc > 2) { fprintf(stderr, "Usage: %s [port]\n", argv[0]); return 1; }
    if (argc == 2) {
        char *end;
        port = strtol(argv[1], &end, 10);
        if (!argv[1][0] || *end || port < 1 || port > 65535) { fprintf(stderr, "Invalid port\n"); return 1; }
    }
    signal(SIGPIPE, SIG_IGN); signal(SIGINT, stop); signal(SIGTERM, stop);
    srand((unsigned int)time(NULL));
    game_init(&game);
    for (int i = 0; i < MAX_CONNECTIONS; ++i) clients[i].fd = clients[i].player = -1;
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { perror("socket"); return 1; }
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_ANY), .sin_port = htons((uint16_t)port) };
    if (set_nonblocking(listener) < 0 || bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(listener, MAX_CONNECTIONS) < 0) { perror("listen/bind"); close(listener); return 1; }
    printf("Snake server listening on port %ld\n", port);
    fflush(stdout);
    long long next_tick = monotonic_ms() + 1000 / game.tps, next_broadcast = monotonic_ms();
    while (running) {
        struct pollfd fds[MAX_CONNECTIONS + 1];
        fds[0] = (struct pollfd){ .fd = listener, .events = POLLIN };
        for (int i = 0; i < MAX_CONNECTIONS; ++i)
            fds[i + 1] = (struct pollfd){ .fd = clients[i].fd, .events = POLLIN | (clients[i].queued ? POLLOUT : 0) };
        long long now = monotonic_ms(), due = next_tick < next_broadcast ? next_tick : next_broadcast;
        int timeout = due > now ? (int)(due - now) : 0;
        if (poll(fds, MAX_CONNECTIONS + 1, timeout) < 0) {
            if (errno == EINTR) continue;
            perror("poll"); break;
        }
        /* Process only connections represented in this poll snapshot before accepting new ones. */
        for (int i = 0; i < MAX_CONNECTIONS; ++i) {
            Connection *c = &clients[i];
            short events = fds[i + 1].revents;
            if (c->fd < 0) continue;
            if (events & (POLLERR | POLLHUP | POLLNVAL)) { disconnect_client(c); continue; }
            if (events & POLLIN) receive_input(c);
            if (c->fd >= 0 && (events & POLLOUT)) send_output(c);
            now = monotonic_ms();
            if (c->fd >= 0 && ((c->player < 0 && now - c->connected_at >= CONNECTION_TIMEOUT_MS) ||
                (c->queued && now - c->queued_at >= CONNECTION_TIMEOUT_MS))) disconnect_client(c);
        }
        if (fds[0].revents & POLLIN) {
            /* Bound accepts too, including rejected connections. */
            for (int k = 0; k < MAX_CONNECTIONS; ++k) {
                int fd = accept(listener, NULL, NULL);
                if (fd < 0) break;
                int slot = -1;
                for (int i = 0; i < MAX_CONNECTIONS; ++i) if (clients[i].fd < 0) { slot = i; break; }
                if (slot < 0 || set_nonblocking(fd) < 0) { close(fd); continue; }
                /* Keep stale snapshots bounded even when a peer stops reading. */
                int send_buffer = 64 * 1024;
                setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
                clients[slot].fd = fd;
                clients[slot].connected_at = monotonic_ms();
            }
        }
        now = monotonic_ms();
        if (now >= next_tick) {
            game_tick(&game);
            next_tick = now + 1000 / game.tps;
        }
        if (now >= next_broadcast) {
            for (int i = 0; i < MAX_CONNECTIONS; ++i)
                if (clients[i].fd >= 0) queue_frame(&clients[i], now);
            next_broadcast = now + BROADCAST_MS;
        }
    }
    for (int i = 0; i < MAX_CONNECTIONS; ++i) disconnect_client(&clients[i]);
    close(listener);
    return 0;
}
