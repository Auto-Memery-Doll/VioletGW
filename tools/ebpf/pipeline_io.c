#include "observe_loop.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <bpf/libbpf.h>

#include "pipeline_io.skel.h"

static const char *SYM_RX = "_ZN3vgw4dpdk8rx_burstEttPP8rte_mbuft";
static const char *SYM_TX = "_ZN3vgw4dpdk8tx_burstEttPP8rte_mbuft";

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [-b binary] [-p pid]\n"
            "  -b  path to vgw (default: build/bin/Src/vgw)\n"
            "  -p  optional PID filter (-1 = all processes, default)\n",
            prog);
}

static int libbpf_print_fn(enum libbpf_print_level level, const char *format,
                           va_list args) {
    if (level == LIBBPF_DEBUG) {
        return 0;
    }
    return vfprintf(stderr, format, args);
}

int main(int argc, char **argv) {
    const char *binary = "build/bin/Src/vgw";
    int pid = -1;
    int opt;

    while ((opt = getopt(argc, argv, "b:p:h")) != -1) {
        switch (opt) {
        case 'b':
            binary = optarg;
            break;
        case 'p':
            pid = atoi(optarg);
            break;
        case 'h':
        default:
            usage(argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }

    if (access(binary, R_OK) != 0) {
        fprintf(stderr, "error: cannot read binary %s: %s\n", binary,
                strerror(errno));
        return 1;
    }

    libbpf_set_print(libbpf_print_fn);

    struct pipeline_io_bpf *skel = pipeline_io_bpf__open();
    if (!skel) {
        fprintf(stderr, "failed to open BPF skeleton\n");
        return 1;
    }

    int err = pipeline_io_bpf__load(skel);
    if (err) {
        fprintf(stderr, "failed to load BPF skeleton: %d\n", err);
        pipeline_io_bpf__destroy(skel);
        return 1;
    }

    LIBBPF_OPTS(bpf_uprobe_opts, rx_opts, .func_name = SYM_RX,
                .retprobe = true);
    skel->links.trace_rx_ret = bpf_program__attach_uprobe_opts(
        skel->progs.trace_rx_ret, pid, binary, 0 /* offset */, &rx_opts);
    if (!skel->links.trace_rx_ret) {
        fprintf(stderr, "failed to attach uretprobe %s: %s\n", SYM_RX,
                strerror(errno));
        pipeline_io_bpf__destroy(skel);
        return 1;
    }

    LIBBPF_OPTS(bpf_uprobe_opts, tx_opts, .func_name = SYM_TX,
                .retprobe = true);
    skel->links.trace_tx_ret = bpf_program__attach_uprobe_opts(
        skel->progs.trace_tx_ret, pid, binary, 0 /* offset */, &tx_opts);
    if (!skel->links.trace_tx_ret) {
        fprintf(stderr, "failed to attach uretprobe %s: %s\n", SYM_TX,
                strerror(errno));
        pipeline_io_bpf__destroy(skel);
        return 1;
    }

    printf("[pipeline] probes: dpdk::rx_burst / tx_burst on %s\n", binary);
    err = observe_loop(bpf_map__fd(skel->maps.counts), "pipeline");
    pipeline_io_bpf__destroy(skel);
    return err;
}
