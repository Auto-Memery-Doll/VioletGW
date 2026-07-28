#include "vmlinux.h"
#include "counts.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, IDX_MAX);
    __type(key, __u32);
    __type(value, __u64);
} counts SEC(".maps");

static __always_inline void add_count(__u32 idx, __u64 delta) {
    __u64 *v = bpf_map_lookup_elem(&counts, &idx);
    if (v) {
        *v += delta;
    }
}

SEC("uretprobe")
int BPF_URETPROBE(trace_rx_ret, long ret) {
    if (ret > 0) {
        add_count(IDX_RX, (__u64)ret);
        add_count(IDX_RX_CALLS, 1);
    }
    return 0;
}

SEC("uretprobe")
int BPF_URETPROBE(trace_tx_ret, long ret) {
    if (ret > 0) {
        add_count(IDX_TX, (__u64)ret);
        add_count(IDX_TX_CALLS, 1);
    }
    return 0;
}
