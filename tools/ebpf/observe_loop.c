#include "observe_loop.h"
#include "counts.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

static volatile sig_atomic_t g_stop;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static int sum_percpu(int map_fd, uint32_t key, uint64_t *out) {
    const int ncpus = libbpf_num_possible_cpus();
    if (ncpus <= 0) {
        return -1;
    }

    uint64_t *values = calloc((size_t)ncpus, sizeof(*values));
    if (!values) {
        return -1;
    }

    if (bpf_map_lookup_elem(map_fd, &key, values) != 0) {
        free(values);
        return -1;
    }

    uint64_t sum = 0;
    for (int i = 0; i < ncpus; ++i) {
        sum += values[i];
    }
    free(values);
    *out = sum;
    return 0;
}

int observe_loop(int map_fd, const char *tag) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("[%s] attached; Ctrl-C to stop\n", tag);
    fflush(stdout);

    const time_t start = time(NULL);
    uint64_t prev_rx = 0;
    uint64_t prev_tx = 0;

    while (!g_stop) {
        sleep(1);

        uint64_t rx = 0;
        uint64_t tx = 0;
        if (sum_percpu(map_fd, IDX_RX, &rx) != 0 ||
            sum_percpu(map_fd, IDX_TX, &tx) != 0) {
            fprintf(stderr, "[%s] map lookup failed: %s\n", tag,
                    strerror(errno));
            return 1;
        }

        printf("[%s] rx=%llu tx=%llu rx_pps=%llu tx_pps=%llu\n", tag,
               (unsigned long long)rx, (unsigned long long)tx,
               (unsigned long long)(rx - prev_rx),
               (unsigned long long)(tx - prev_tx));
        fflush(stdout);
        prev_rx = rx;
        prev_tx = tx;
    }

    time_t elapsed = time(NULL) - start;
    if (elapsed <= 0) {
        elapsed = 1;
    }

    uint64_t rx = 0;
    uint64_t tx = 0;
    uint64_t rx_calls = 0;
    uint64_t tx_calls = 0;
    if (sum_percpu(map_fd, IDX_RX, &rx) != 0 ||
        sum_percpu(map_fd, IDX_TX, &tx) != 0 ||
        sum_percpu(map_fd, IDX_RX_CALLS, &rx_calls) != 0 ||
        sum_percpu(map_fd, IDX_TX_CALLS, &tx_calls) != 0) {
        fprintf(stderr, "[%s] final map lookup failed: %s\n", tag,
                strerror(errno));
        return 1;
    }

    printf("[%s] done elapsed_s=%lld rx=%llu tx=%llu avg_rx_pps=%llu "
           "avg_tx_pps=%llu rx_calls=%llu tx_calls=%llu\n",
           tag, (long long)elapsed, (unsigned long long)rx,
           (unsigned long long)tx, (unsigned long long)(rx / (uint64_t)elapsed),
           (unsigned long long)(tx / (uint64_t)elapsed),
           (unsigned long long)rx_calls, (unsigned long long)tx_calls);
    fflush(stdout);
    return 0;
}
