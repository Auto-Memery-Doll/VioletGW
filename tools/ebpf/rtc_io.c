#include "observe_loop.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <bpf/libbpf.h>

#include "rtc_io.skel.h"

static const char *SYM_RECV = "_ZN3vgw9DpdkNetif10recv_burstEPP8rte_mbufj";
static const char *SYM_SEND = "_ZN3vgw9DpdkNetif10send_burstEPP8rte_mbufj";

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

    struct rtc_io_bpf *skel = rtc_io_bpf__open();
    if (!skel) {
        fprintf(stderr, "failed to open BPF skeleton\n");
        return 1;
    }

    int err = rtc_io_bpf__load(skel);
    if (err) {
        fprintf(stderr, "failed to load BPF skeleton: %d\n", err);
        rtc_io_bpf__destroy(skel);
        return 1;
    }

    LIBBPF_OPTS(bpf_uprobe_opts, recv_opts, .func_name = SYM_RECV,
                .retprobe = true);
    skel->links.trace_recv_ret = bpf_program__attach_uprobe_opts(
        skel->progs.trace_recv_ret, pid, binary, 0 /* offset */, &recv_opts);
    if (!skel->links.trace_recv_ret) {
        fprintf(stderr, "failed to attach uretprobe %s: %s\n", SYM_RECV,
                strerror(errno));
        rtc_io_bpf__destroy(skel);
        return 1;
    }

    LIBBPF_OPTS(bpf_uprobe_opts, send_opts, .func_name = SYM_SEND,
                .retprobe = true);
    skel->links.trace_send_ret = bpf_program__attach_uprobe_opts(
        skel->progs.trace_send_ret, pid, binary, 0 /* offset */, &send_opts);
    if (!skel->links.trace_send_ret) {
        fprintf(stderr, "failed to attach uretprobe %s: %s\n", SYM_SEND,
                strerror(errno));
        rtc_io_bpf__destroy(skel);
        return 1;
    }

    printf("[rtc] probes: recv_burst / send_burst on %s\n", binary);
    err = observe_loop(bpf_map__fd(skel->maps.counts), "rtc");
    rtc_io_bpf__destroy(skel);
    return err;
}
