# Multiplayer terminal Snake

A C and ncurses multiplayer game with two-player local play on one keyboard, or up to eight players over TCP.

## Local two-player game (no server)

```sh
make client
./client --local
```

Both people share one terminal and keyboard. **Player 1 uses WASD; Player 2 uses the arrow keys.** No server, connection, or username prompt is needed.

In the lobby, use **A/D** to cycle Player 1's colour and **Left/Right arrows** to cycle Player 2's colour. Press **M** to choose max score or last worm standing, then **G** to start. During play, **P** pauses/resumes, **+/-** changes speed, and **.** quits. After a round, **G** restarts. All power-ups, collision rules, scoring, and mode-specific respawns work as in online play.

Use a terminal of at least **80 columns × 31 rows** so both players can see the entire arena and their own scores and power-up timers. Shrinking the terminal below that size pauses the round; resize it and press **P** to resume.

## Build and run

On Ubuntu/Debian:

```sh
sudo apt-get install build-essential libncurses-dev python3
make
./server
```

In a separate terminal for each player:

```sh
./client 127.0.0.1
```

Use the server's IPv4 address when playing across machines. The default TCP port is **5500**; an optional port can be passed to both programs (`./server 5501`, `./client 127.0.0.1 5501`). Enter a nickname to join. All players need this version of the client and server; the protocol is incompatible with older builds.

The first player becomes host. If the host disconnects, a remaining player takes over. The lobby shows player numbers, names, colours, the host, game type, and speed.

## Controls

| Key | Action | Who / when |
| --- | --- | --- |
| `1`–`8` | Choose your colour | Everyone, in the lobby or after a round |
| `M` | Toggle game type | Host, in the lobby or after a round |
| `G` | Start or restart a round | Host |
| `WASD` / arrows | Steer | Living players |
| `P` | Pause or resume the whole game | Everyone during a round, including spectators |
| `+` / `=` / `-` | Increase / decrease shared speed | Everyone, including while paused |
| `.` | Disconnect and quit | Everyone |

Speed ranges from **2–20 ticks per second**, with a default of **5**. Pause freezes movement, the starting countdown, power-up timers, and respawn timers. The HUD shows each player's length and score, your power-up/respawn timers, and the shared speed and pause state.

A terminal of **80 columns × 37 rows** shows the full arena and all eight players. Smaller terminals (minimum 60 × 20) scroll the arena around your snake and keep the controls and scoreboard visible. Heads are `@`, powered heads are bold yellow-on-magenta `C` characters, and body segments show player numbers so players remain distinguishable even with the same colour or without colour support.

## Rules

- **Wraparound arena:** there are no internal walls. Crossing any edge places your head on the opposite edge in that same movement step; the rest of the body follows segment by segment. The dotted outline marks the wrap boundary, not a wall.
- **Safer starts and respawns:** placement checks the entire body and 20 clear movement steps ahead (about 10 ticks even with an apple boost), and avoids other snakes' projected straight-ahead routes. If no safe route exists, respawning waits and retries. Players changing direction can still create collisions.

- **Normal snake collisions:** if your head hits another snake, you lose half your length and the other snake gains that length and score. The losing snake turns around, and both snakes stop for that movement step so they do not overlap. Odd lengths transfer the rounded-down half; a one-segment snake dies on contact.
- **Apples (`O`):** add **2 length and 2 score**, then move you **twice per tick for the next 10 ticks**. Every movement step checks collisions and food, so boosts cannot tunnel through snakes or skip collisions across an edge. Another apple refreshes the timer to 10.
- **Pacman berries (`*`):** let you eat other snakes on contact for **30 ticks**. You gain half the victim's length and that much score, and the victim shrinks and turns around. Berries refresh the timer; they do not add length themselves. Your own body still kills you.
- **Simultaneous contact:** a single powered snake wins a head-on collision. With equal power, head-on collisions halve both snakes and award no points; equal power in a head-to-body collision uses the normal collision rule. A pair can transfer length only once per tick. If multiple snakes eat the same victim simultaneously, they share its lost half equally (any remainder is discarded).
- **Death:** hitting your own body kills you and removes **25% of your score, rounded up**. Score never goes below zero. Moving into your own vacating tail is allowed.
- **Score:** starts at zero and tracks length gained from apples and other snakes, minus death penalties. Losing length in a collision does not remove score. Starting and respawn lengths do not award points.

### Max score

The first player to reach **150 accumulated points** wins. If several reach the target in the same tick, the highest score wins; equal top scores draw. Dead players automatically respawn after **10 unpaused ticks**, with eight segments and their remaining score. If no safe spawn exists, the server retries each tick. Players joining mid-round can play immediately.

### Last worm standing

No respawns. The last living player wins; if everyone dies in the same tick, the round is a draw. A single-player round continues until that player dies. Players joining mid-round spectate until the next round. Disconnecting removes a player from contention. After a round, the host can change game type and press `G` to restart; everyone can choose colours again.

## Configuration and verification

Edit `game_config.h` and rebuild to change the board, target score, power-up durations, or respawn delay. `game.c` contains the rules independently of sockets and terminal rendering. Networking uses versioned, fixed-size frames with network byte order, partial-read/write handling, bounded connection queues, and timeouts for stalled peers. The client performs all ncurses work on one thread.

```sh
make test-local # Local controls and terminal tests; no sockets or server required
make test       # All tests, including 100,000 simulated ticks and TCP multiplayer
make sanitize   # Rebuild and run with AddressSanitizer + UndefinedBehaviorSanitizer
make clean
make            # Return to the normal optimized build
```

Tests require Python 3, loopback socket access, and a working pseudoterminal. They exercise collision transfers, power-up timing, capacity limits, score penalties, both modes, respawns, lobby controls, paused timers, host changes, fragmented/coalesced TCP messages, slow clients, and terminal resizing/cleanup.

Adapted from [manhminno/Multiplayer-Snake-Game](https://github.com/manhminno/Multiplayer-Snake-Game); earlier versions were developed with help from Gemini3.
