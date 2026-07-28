#pragma once

#include <stdint.h>

/* Block until SIGINT/SIGTERM; print 1 Hz stats and a final summary. */
int observe_loop(int map_fd, const char *tag);
