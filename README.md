# Object-detection
Edge-based object detection &amp; tracking (C, ncurses)

A small C-based surveillance/demo project that scans image edges for coverage, detects connected components, computes object centroids, and visualizes results using ncurses.

## Features
- Edge scanning (serial + parallel) and occupancy reporting
- Connected-component detection → one centroid per object
- Simple tracking and demo-mode simulated objects

## Requirements
- gcc (or compatible C compiler)
- pthreads (POSIX threads)
- ncurses development library (e.g., libncurses-dev)

## Build & Run
- Build: `make surv` or `gcc -Wall -Wextra -pthread surv.c checker.c -o surv -lncurses -lm`
- Run demo: `make surv-run` or `./surv -d`
- Run without demo: `./surv`

## Files
- `surv.c` — edge scanning, detection, UI
- `checker.c`, `checker.h` — simulation, check() API, detection list management
- `Makefile` — build and run targets

## Notes
- The demo option is `-d` / `--demo` and spawns simulated moving objects to exercise detection and tracking.
