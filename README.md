# Multiplayer Snake Game (Stabilized Version)

This is a lightweight, C-based multiplayer Snake game designed for team socials and casual play. It uses TCP sockets for networking and ncurses for the terminal display. 

This version has been rewritten from the ground up to prevent segmentation faults, remove complex login requirements, and ensure fair gameplay via a tick-based server architecture.

## Features
*   **Capacity:** Supports up to 8 concurrent players (Configurable).
*   **Stability:** Tick-based physics engine ensures one laggy player does not freeze the game for others.
*   **No Login:** Simply enter a nickname and play.
*   **Visuals:** Dynamic color assignment for different snakes.
*   **Game Modes:** Lobby system with a Host start mechanic.

## Prerequisites
You need a Linux environment with GCC and the Ncurses library installed.

```bash
sudo apt-get install libncurses5-dev
```


Compilation

```
gcc server.c -o server -lpthread
```

# Compile Client
```
gcc client.c -o client -lncurses -lpthread
```

Lobby: When you join, you enter a "Waiting" state.
Starting: The First Player to join is designated as the Host.
Host must press G to start the round for everyone.
Movement: W (Up), A (Left), S (Down), D (Right).
Quit: Press . to disconnect safely.
Winning: The first player to reach the Score Limit (Default: 15) wins. The Host can then press G to restart the game.
Configuration
To change game settings, edit game_config.h and recompile both server and client.

```
WIN_LENGTH: Points required to win (Default: 15).
MAX_PLAYERS: Max concurrent connections (Default: 8).
TICK_RATE_US: Game speed in microseconds (Default: 100000 = 10 FPS).
```


# Attribution:
Adapted based on https://github.com/manhminno/Multiplayer-Snake-Game with help from Gemini3

