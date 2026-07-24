# Remove legacy balance + POSIX SHM control plane

Date: 2026-07-24  
Status: approved (conversation)

## Goal

1. Delete unused `src/balance/` (Engine / HeartbeatMonitor / string-IP algos).
2. Add a **versioned shared-memory config block** so a future Go control plane can publish upstream list + balance policy; the data plane polls and applies into `UpstreamTable` (hot path never reads SHM).

## Non-goals

- Go control-plane process
- Heartbeat / health checks in C++
- Command queue / ring IPC
- Hot-path `mmap` reads in `pick`
- Multi-VIP layouts

## SHM layout (C ABI)

POSIX name: `/flow_gateway_cp` (overridable later via config).

Packed, little-endian fields; documented in a pure-C header for Go `cgo`:

```c
#define FG_CP_SHM_MAGIC   0x46474350u  /* 'FGCP' */
#define FG_CP_SHM_MAX_EP  64

struct fg_cp_endpoint {
    uint32_t ip_be;
    uint16_t port;   /* host order */
    uint16_t _pad;
};

struct fg_cp_shm {
    uint32_t magic;
    uint32_t version;  /* bump last after a full publish */
    uint8_t  policy;   /* 0=mod, 1=rr */
    uint8_t  flags;
    uint16_t count;
    struct fg_cp_endpoint endpoints[FG_CP_SHM_MAX_EP];
};
```

**Publish (writer):** fill `endpoints[0..count)`, set `policy`, then increment/store `version` (release).  
**Apply (reader):** if `magic` ok and `version != last_applied`, copy snapshot under the observed version, call `UpstreamTable::set` + `set_policy`, record `last_applied`.

Brief torn reads across version bump are acceptable; next poll corrects.

## Data-plane module: `src/control/`

- `CpShm`: create-or-attach, optional seed from `config.hpp`, `poll_apply(UpstreamTable*)`
- `main`: after building `UpstreamTable`, attach/seed SHM; in worker loop (same cadence as session expire) call `poll_apply`
- Hot path unchanged: only `UpstreamTable` atomics

## Remove `balance/`

- Delete `src/balance/**`
- Drop `balance` from `src/CMakeLists.txt` / `flow_gw` link line
- Confirm no remaining includes

## Tests

- gtest: create anonymous/named SHM in test, write layout, `poll_apply`, assert table membership/policy
- Existing unit suite still green

## Success criteria

- No `balance` sources in build
- `ctest -L unit` passes including control/SHM tests
- `flow_gw` links `control` + `upstream`; poll path wired
