# Datapath RTC + Pipeline (P0) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add gflags-selected datapath modes with enum+union static polymorphism; ship **P0** = Pipeline (default) + single-worker RTC (direct NIC), without virtual functions.

**Architecture:** `DatapathConfig` (mode + union params) fills at init from gflags; `DpdkNetif` switches `recv_burst`/`send_burst` on `mode_`; Pipeline keeps soft rings + I/O lcores; RTC with `workers==1` calls `rte_eth_rx/tx_burst` on the worker thread. P1 (HwRss) / P2 (SoftRr) are outlined only — blocked on SessionTable concurrency.

**Tech Stack:** C++17, DPDK, gflags (FetchContent), existing `vgw_dpdk_io` / gtest

**Spec:** `docs/superpowers/specs/2026-07-27-datapath-rtc-pipeline-design.md`

## Global Constraints

- No virtual functions on datapath hot path (`enum` + `switch` + `union` only)
- Init-time mode select only; default `--datapath_mode=pipeline`
- `rtc_workers == 1` ⇒ direct RTC, never launch distributor
- Soft multi-worker uses RR (P2), not hash; English-only in code
- Do not change L4 forward / session semantics in P0
- No git commit unless user asks (override plan “Commit” steps: stage only / skip commit)

---

### Task 1: `DatapathConfig` types + resolve helpers

**Files:**
- Create: `src/dpdk/datapath_config.hpp`
- Create: `src/dpdk/datapath_config.cpp` (resolve + string parse only; flags in Task 2)
- Modify: `src/dpdk/CMakeLists.txt` — add `datapath_config.cpp` to `vgw_dpdk_io`
- Test: `test/unit/datapath_config_test.cpp`

**Interfaces:**
- Produces:
  - `enum class DatapathMode : uint8_t { Pipeline, Rtc }`
  - `enum class RtcSteer : uint8_t { Auto, HwRss, SoftRr }`
  - `enum class RtcResolvedPath : uint8_t { Direct, HwRss, SoftRr }`
  - `struct PipelineParams`, `struct RtcParams`, `struct DatapathConfig` as in spec
  - `RtcResolvedPath resolve_rtc_path(const RtcParams& rtc, bool hw_rss_usable)`
  - `bool parse_datapath_mode(const char* s, DatapathMode* out)`
  - `bool parse_rtc_steer(const char* s, RtcSteer* out)`
  - `DatapathConfig datapath_config_defaults()` — pipeline defaults from `dpdk/config.hpp`

- [ ] **Step 1: Write failing unit test**

```cpp
#include "dpdk/datapath_config.hpp"
#include <gtest/gtest.h>

TEST(DatapathConfigTest, DefaultsArePipeline) {
    auto c = vgw::datapath_config_defaults();
    EXPECT_EQ(c.mode, vgw::DatapathMode::Pipeline);
}

TEST(DatapathConfigTest, WorkersOneAlwaysDirect) {
    vgw::RtcParams rtc{};
    rtc.steer = vgw::RtcSteer::SoftRr;
    rtc.workers = 1;
    EXPECT_EQ(vgw::resolve_rtc_path(rtc, /*hw_rss_usable=*/false),
              vgw::RtcResolvedPath::Direct);
    EXPECT_EQ(vgw::resolve_rtc_path(rtc, /*hw_rss_usable=*/true),
              vgw::RtcResolvedPath::Direct);
}

TEST(DatapathConfigTest, AutoPicksHwOrSoftWhenMultiWorker) {
    vgw::RtcParams rtc{};
    rtc.steer = vgw::RtcSteer::Auto;
    rtc.workers = 2;
    EXPECT_EQ(vgw::resolve_rtc_path(rtc, true), vgw::RtcResolvedPath::HwRss);
    EXPECT_EQ(vgw::resolve_rtc_path(rtc, false), vgw::RtcResolvedPath::SoftRr);
}

TEST(DatapathConfigTest, ParseModeAndSteer) {
    vgw::DatapathMode m{};
    ASSERT_TRUE(vgw::parse_datapath_mode("rtc", &m));
    EXPECT_EQ(m, vgw::DatapathMode::Rtc);
    vgw::RtcSteer s{};
    ASSERT_TRUE(vgw::parse_rtc_steer("soft_rr", &s));
    EXPECT_EQ(s, vgw::RtcSteer::SoftRr);
}
```

- [ ] **Step 2: Run test — expect fail (missing headers)**

```bash
cmake --build build -j$(nproc) --target datapath_config_test
```

Expected: configure/link error or missing target.

- [ ] **Step 3: Implement `datapath_config.hpp` / `.cpp`**

Header sketch (exact enums/structs):

```cpp
#pragma once
#include <cstdint>

namespace vgw {

enum class DatapathMode : uint8_t { Pipeline = 0, Rtc = 1 };
enum class RtcSteer : uint8_t { Auto = 0, HwRss = 1, SoftRr = 2 };
enum class RtcResolvedPath : uint8_t { Direct = 0, HwRss = 1, SoftRr = 2 };

struct PipelineParams {
    uint16_t ring_size = 0;
    bool launch_rx_lcore = true;
    bool launch_tx_lcore = true;
    int tx_ring_full_sleep_us = 10;
};

struct RtcParams {
    RtcSteer steer = RtcSteer::Auto;
    uint16_t workers = 1;
    uint16_t rx_queues = 0;
    uint16_t tx_queues = 0;
    uint16_t dist_ring_size = 0;
};

struct DatapathConfig {
    DatapathMode mode = DatapathMode::Pipeline;
    uint32_t port_mask = 0;
    uint16_t worker_port = 0;
    union {
        PipelineParams pipeline;
        RtcParams rtc;
    } u{};
};

DatapathConfig datapath_config_defaults();
RtcResolvedPath resolve_rtc_path(const RtcParams& rtc, bool hw_rss_usable);
bool parse_datapath_mode(const char* s, DatapathMode* out);
bool parse_rtc_steer(const char* s, RtcSteer* out);

}  // namespace vgw
```

`resolve_rtc_path`: if `workers <= 1` return `Direct`; else if steer `HwRss` return `HwRss`; else if `SoftRr` return `SoftRr`; else (`Auto`) return `hw_rss_usable ? HwRss : SoftRr`.

`datapath_config_defaults`: `mode=Pipeline`, `port_mask=config::DPDK_vaild_port_marks`, `u.pipeline` from `IO_*` constants.

- [ ] **Step 4: Wire CMake + test target**

In `src/dpdk/CMakeLists.txt`:

```cmake
add_library(vgw_dpdk_io netif.cpp datapath_config.cpp)
```

In `test/unit/CMakeLists.txt`:

```cmake
vgw_add_gtest(datapath_config_test datapath_config_test.cpp)
target_link_libraries(datapath_config_test PRIVATE vgw_dpdk_io)
```

- [ ] **Step 5: Build and run**

```bash
cmake -S . -B build -G Ninja && cmake --build build -j$(nproc)
ctest --test-dir build -R DatapathConfigTest --output-on-failure
```

Expected: all `DatapathConfigTest.*` PASS.

---

### Task 2: gflags + `datapath_config_from_flags`

**Files:**
- Create: `src/dpdk/datapath_flags.hpp`
- Create: `src/dpdk/datapath_flags.cpp`
- Modify: `CMakeLists.txt` (root) — FetchContent gflags
- Modify: `src/dpdk/CMakeLists.txt` — link gflags; add `datapath_flags.cpp`
- Modify: `src/main.cpp` / `src/vgw.cpp` — parse flags before EAL (Task 3 finishes wiring)
- Test: extend `test/unit/datapath_config_test.cpp` with from-flags helpers if testable without full gflags init; or small test that sets `FLAGS_*` then calls `datapath_config_from_flags()`

**Interfaces:**
- Consumes: `DatapathConfig`, parsers from Task 1
- Produces:
  - `void register_datapath_flags()` or DEFINE in `.cpp` with declarations in `.hpp`
  - `DatapathConfig datapath_config_from_flags()`
  - Flags: `datapath_mode`, `datapath_port_mask`, `datapath_worker_port`, `io_ring_size`, `io_rx_lcore`, `io_tx_lcore`, `io_tx_ring_full_sleep_us`, `rtc_steer`, `rtc_workers`, `rtc_dist_ring_size`

- [ ] **Step 1: Add gflags to root `CMakeLists.txt`**

```cmake
find_package(gflags QUIET)
if(NOT gflags_FOUND)
    FetchContent_Declare(
        gflags
        GIT_REPOSITORY https://github.com/gflags/gflags.git
        GIT_TAG v2.2.2
        GIT_SHALLOW TRUE
    )
    set(GFLAGS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(GFLAGS_BUILD_gflags_LIB ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(gflags)
endif()
```

Link `gflags::gflags` (or `gflags`) PRIVATE to `vgw_dpdk_io`.

- [ ] **Step 2: Define flags + `datapath_config_from_flags()`**

```cpp
// datapath_flags.cpp (sketch)
DEFINE_string(datapath_mode, "pipeline", "pipeline | rtc");
DEFINE_uint64(datapath_port_mask, 0, "0 = use dpdk/config default");
DEFINE_uint32(datapath_worker_port, 0, "worker primary port id");
DEFINE_uint32(io_ring_size, 0, "0 = config::IO_RING_SIZE");
DEFINE_bool(io_rx_lcore, true, "pipeline: launch RX lcore");
DEFINE_bool(io_tx_lcore, true, "pipeline: launch TX lcore");
DEFINE_int32(io_tx_ring_full_sleep_us, 10, "pipeline TX ring full sleep");
DEFINE_string(rtc_steer, "auto", "auto | hw_rss | soft_rr");
DEFINE_uint32(rtc_workers, 1, "RTC worker count; 1 = direct");
DEFINE_uint32(rtc_dist_ring_size, 0, "soft_rr ring size; 0 = default");

DatapathConfig datapath_config_from_flags() {
    DatapathConfig c = datapath_config_defaults();
    DatapathMode mode{};
    if (!parse_datapath_mode(FLAGS_datapath_mode.c_str(), &mode)) {
        rte_exit(EXIT_FAILURE, "bad --datapath_mode=%s\n",
                 FLAGS_datapath_mode.c_str());
    }
    c.mode = mode;
    if (FLAGS_datapath_port_mask != 0) {
        c.port_mask = static_cast<uint32_t>(FLAGS_datapath_port_mask);
    }
    c.worker_port = static_cast<uint16_t>(FLAGS_datapath_worker_port);
    if (mode == DatapathMode::Pipeline) {
        c.u.pipeline = {};
        c.u.pipeline.ring_size = static_cast<uint16_t>(FLAGS_io_ring_size);
        c.u.pipeline.launch_rx_lcore = FLAGS_io_rx_lcore;
        c.u.pipeline.launch_tx_lcore = FLAGS_io_tx_lcore;
        c.u.pipeline.tx_ring_full_sleep_us = FLAGS_io_tx_ring_full_sleep_us;
    } else {
        c.u.rtc = {};
        RtcSteer steer{};
        if (!parse_rtc_steer(FLAGS_rtc_steer.c_str(), &steer)) {
            rte_exit(EXIT_FAILURE, "bad --rtc_steer=%s\n",
                     FLAGS_rtc_steer.c_str());
        }
        c.u.rtc.steer = steer;
        c.u.rtc.workers = static_cast<uint16_t>(FLAGS_rtc_workers);
        if (c.u.rtc.workers == 0) {
            c.u.rtc.workers = 1;
        }
        c.u.rtc.dist_ring_size =
            static_cast<uint16_t>(FLAGS_rtc_dist_ring_size);
    }
    return c;
}
```

- [ ] **Step 3: Unit test with FLAGS assignment**

```cpp
TEST(DatapathConfigTest, FromFlagsRtcDirect) {
    FLAGS_datapath_mode = "rtc";
    FLAGS_rtc_workers = 1;
    FLAGS_rtc_steer = "soft_rr";
    auto c = vgw::datapath_config_from_flags();
    EXPECT_EQ(c.mode, vgw::DatapathMode::Rtc);
    EXPECT_EQ(c.u.rtc.workers, 1);
    EXPECT_EQ(vgw::resolve_rtc_path(c.u.rtc, false),
              vgw::RtcResolvedPath::Direct);
}
```

- [ ] **Step 4: Build + ctest**

```bash
cmake --build build -j$(nproc)
ctest --test-dir build -R DatapathConfigTest --output-on-failure
```

Expected: PASS.

---

### Task 3: Refactor `DpdkNetif` for Pipeline vs Direct RTC (P0)

**Files:**
- Modify: `src/dpdk/netif.hpp`
- Modify: `src/dpdk/netif.cpp`
- Modify: `src/vgw.cpp` — `init` uses `datapath_config_from_flags()` + `get_netif(cfg.worker_port)`
- Modify: `src/main.cpp` — `gflags::ParseCommandLineFlags` before `app.init` / EAL
- Modify: `src/dpdk` EAL `init` argv handling so leftover args reach EAL

**Interfaces:**
- Consumes: `DatapathConfig`, `datapath_config_from_flags`
- Produces:
  - `DpdkNetifManager::init(const DatapathConfig& cfg)`
  - `DpdkNetif::recv_burst` / `send_burst` branch on `mode_`
  - P0 RTC path: `queue_id_ == 0`, direct `dpdk::rx_burst` / `tx_burst`
  - Pipeline path: existing ring + `run_recv` / `run_send` when `launch_*`

- [ ] **Step 1: Extend `DpdkNetif` private state**

```cpp
class DpdkNetif {
  // public API unchanged: recv_burst, send_burst, free_burst, port()
private:
    DatapathMode mode_ = DatapathMode::Pipeline;
    uint16_t port_id_ = 0;
    uint16_t queue_id_ = 0;  // RTC direct / later per-worker
    union {
        struct {
            Ring<rte_mbuf>::ptr rx_ring;
            Ring<rte_mbuf>::ptr tx_ring;
            bool stop;
            int tx_ring_full_sleep_us;
        } pipeline;
        struct {
            // P0 Direct: no extra fields
            // P2 SoftRr: ring ptr filled later
            void* soft_rx_ring;  // nullptr in P0
        } rtc;
    } state_{};
};
```

Activate only the active union member in `init` (placement / assign carefully: default-construct rings only for Pipeline).

- [ ] **Step 2: Implement `recv_burst` / `send_burst` switch**

```cpp
unsigned DpdkNetif::recv_burst(rte_mbuf** pkts, unsigned n) {
    switch (mode_) {
    case DatapathMode::Pipeline:
        return state_.pipeline.rx_ring->pop_burst(pkts, n);
    case DatapathMode::Rtc:
        return static_cast<unsigned>(
            dpdk::rx_burst(port_id_, queue_id_, pkts,
                           static_cast<uint16_t>(n)));
    }
    return 0;
}

unsigned DpdkNetif::send_burst(rte_mbuf** pkts, unsigned n) {
    switch (mode_) {
    case DatapathMode::Pipeline: {
        unsigned sent = 0;
        while (sent < n) {
            unsigned en = state_.pipeline.tx_ring->push_burst(
                pkts + sent, n - sent);
            if (en == 0) {
                usleep(state_.pipeline.tx_ring_full_sleep_us);
                continue;
            }
            sent += en;
        }
        return sent;
    }
    case DatapathMode::Rtc: {
        unsigned sent = static_cast<unsigned>(
            dpdk::tx_burst(port_id_, queue_id_, pkts,
                           static_cast<uint16_t>(n)));
        return sent;
    }
    }
    return 0;
}
```

- [ ] **Step 3: Manager `init(const DatapathConfig& cfg)`**

- Store `cfg`.  
- For each port in `cfg.port_mask`: create netif, set `mode_`, `port_id_`.  
- If `Pipeline`: create rings, optionally `rte_eal_remote_launch` RX/TX (use `cfg.u.pipeline`).  
- If `Rtc`: call `resolve_rtc_path`; **P0 only supports `Direct`** — if resolved `HwRss`/`SoftRr`, `SPDLOG_ERROR` / `rte_exit` with message “P1/P2 not implemented; use --rtc_workers=1”.  
- Direct: do **not** launch I/O lcores; `queue_id_=0`.

Keep old `init()` / `init(IoOptions)` as wrappers that build a Pipeline `DatapathConfig` for compatibility, or delete if all callers updated.

- [ ] **Step 4: Wire `main` + `VioletGW::init`**

```cpp
// main.cpp
int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, /*remove_flags=*/true);
    vgw::VioletGW app;
    if (const int rc = app.init(argc, argv)) {
        return rc;
    }
    return app.run();
}
```

```cpp
// VioletGW::init
auto cfg = datapath_config_from_flags();
dpdk::init(argc, argv);
dpdk_netif_mg()->init(cfg);
netif_ = dpdk_netif_mg()->get_netif(cfg.worker_port);
```

- [ ] **Step 5: Build unit tests + smoke compile `vgw`**

```bash
cmake --build build -j$(nproc)
ctest --test-dir build -L unit --output-on-failure
```

Expected: 23+ DatapathConfig tests PASS; `vgw` links.

Manual (optional, needs NIC):  
`--datapath_mode=pipeline` (default) and `--datapath_mode=rtc --rtc_workers=1`.

---

### Task 4: Docs touch-up (P0)

**Files:**
- Modify: `docs/knowledge/RtC & Pipeline.md` — mention gflags modes + single-worker RTC  
- Modify: `README.md` — example flags  
- Modify: `docs/superpowers/specs/2026-07-27-datapath-rtc-pipeline-design.md` — note P0 shipped / P1–P2 pending if desired

- [ ] **Step 1: Add short “Runtime modes” subsection to knowledge + README examples**

```bash
# Pipeline (default)
./build/bin/Src/vgw -l 0-2 --datapath_mode=pipeline

# Single-core RTC
./build/bin/Src/vgw -l 0 --datapath_mode=rtc --rtc_workers=1
```

(Adjust EAL/`gflags` order to match implementation.)

- [ ] **Step 2: Verify docs paths match binary location**

---

### Task 5 (later): P1 HwRss — OUT OF P0 SCOPE

**Blocked on:** SessionTable multi-thread policy (spec Open issues).

When unblocked: multi queue configure + RSS, N worker lcores, `queue_id=i`, no distributor.

### Task 6 (later): P2 SoftRr — OUT OF P0 SCOPE

**Blocked on:** shared SessionTable (RR has no flow affinity).

When unblocked: distributor lcore, per-worker rings, RR index, worker TX (+ spinlock if 1 TXQ).

---

## Spec coverage (self-review)

| Spec item | Task |
|-----------|------|
| enum+union config | Task 1 |
| gflags | Task 2 |
| Pipeline preserved | Task 3 |
| RTC workers==1 direct | Task 3 |
| No vtable | Task 3 |
| Soft RR / HwRss | Task 5–6 (deferred) |
| Default pipeline | Task 1–2 defaults |

## Placeholder scan

None intentional; P1/P2 explicitly deferred with blockers named.
