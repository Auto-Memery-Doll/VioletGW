## 2026-07-27 P0 datapath final-review fixes

- Split VGW and EAL command-line arguments before gflags parsing. Only VGW
  long flags, `--flagfile`, and gflags help/version flags enter gflags; EAL
  arguments are forwarded unchanged to `rte_eal_init`.
- Added pre-narrowing limits checks for port mask, worker port, pipeline and
  RTC ring sizes, and rejected negative TX-ring sleep intervals.
- Warn in pipeline mode when non-default RTC-only flags are supplied, restore
  gflags in the direct-RTC unit test, and cap RTC RX/TX burst requests to
  `uint16_t` maximum.

### Test output

```text
$ ./build/bin/Src/vgw --datapath_mode=pipeline -l 0 --no-huge --no-pci --help
gflags help displayed successfully; `-l`, `--no-huge`, and `--no-pci` did not
cause gflags to abort.

$ ctest --test-dir build -L unit --output-on-failure
100% tests passed, 0 tests failed out of 35
Total Test time (real) = 0.14 sec
```
