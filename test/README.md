# Tests

## Layout

```text
test/
  unit/          # gtest (ctest -L unit) — modules + VioletGW pipeline
  dpdk/          # EAL / mbuf smokes (manual); fwd_loop_smoke uses VioletGW
```

Gateway datapath wiring lives in `src/vgw.*` (`VioletGW`). Tests call
`init_pipeline()` / `handle()` instead of re-implementing forwarder setup.
Lab addresses come from `src/config.hpp` via `test/dpdk/mbuf_fixture.hpp`.

## Unit tests

```bash
cmake -S . -B build -G Ninja
cmake --build build -j$(nproc)
ctest --test-dir build -L unit --output-on-failure
```

## DPDK smokes

```bash
./build/bin/Test/dpdk/eal_smoke -l 0 --no-huge --no-pci
./build/bin/Test/dpdk/fwd_loop_smoke -l 0 --no-huge --no-pci --no-shconf
```
