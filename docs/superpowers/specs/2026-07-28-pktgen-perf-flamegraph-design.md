# Design: pktgen profile mode (perf + FlameGraph)

Date: 2026-07-28  
Status: approved (conversation) — implement via `PROFILE=1` on `run.sh`

## Goal

Add an optional **profile mode** to the physical-NIC stress harness so engineers can capture a **CPU flame graph of `vgw`** during the **measurement window** of a single datapath mode × single case, without changing the default M01–M06 matrix behavior.

## Decisions

| Topic | Choice |
|-------|--------|
| When | Separate profile run (`PROFILE=1`), not every matrix case |
| Toolchain | `perf record` + Brendan Gregg FlameGraph (`stackcollapse-perf.pl` + `flamegraph.pl`) |
| Sample window | Measurement segment only (after Lua warmup stop/restart) |
| Integration | Branch inside `tools/pktgen/run.sh` (recommended); optional helper script |
| Target PID | `vgw` only (not pktgen) |

## Usage

```bash
sudo PROFILE=1 STRESS_DATAPATH_MODES=pipeline PROFILE_CASE=M01 ./tools/pktgen/run.sh
sudo PROFILE=1 STRESS_DATAPATH_MODES=rtc PROFILE_CASE=M01 ./tools/pktgen/run.sh
```

Constraints when `PROFILE=1`:

- Exactly one entry in `STRESS_DATAPATH_MODES`
- `PROFILE_CASE` must be one of M01–M06
- Missing `perf` or FlameGraph scripts → hard fail with install hint

## Env knobs

| Env | Default | Meaning |
|-----|---------|---------|
| `PROFILE` | `0` | `1` enables profile mode |
| `PROFILE_CASE` | `M01` | Case id to run |
| `PERF_FREQ` | `99` | `perf record -F` |
| `PERF_CALL_GRAPH` | `dwarf` | `dwarf` or `fp` |
| `PERF_WARMUP_SLACK_SEC` | `1` | Extra sleep after warmup before `perf` (covers Lua stop/delay/start) |
| `FLAMEGRAPH_DIR` | `$DEV_HOME/FlameGraph` | Directory containing FlameGraph scripts |

Reuse existing: `STRESS_SECONDS`, `STRESS_WARMUP`, `STRESS_DATAPATH_MODES`, NIC/bind knobs.

## Timing

Existing Lua pattern:

1. `start` → delay(warmup) → `stop` → delay(200ms) → read baseline counters  
2. `start` → delay(measure) → `stop` → compute deltas → `PKTGEN_SUMMARY`

Profile alignment:

```text
t=0                         start pktgen (Lua)
… STRESS_WARMUP …           warmup (no perf)
+ PERF_WARMUP_SLACK_SEC     cover stop / delay(200) / start
                            perf record -p $VGW_PID for STRESS_SECONDS
…                           pktgen finishes; build SVG + report
```

## Artifacts

Under `tools/pktgen/out/perf/<mode>_<case>/` (gitignored via `out/`):

- `perf.data`
- `perf.script`
- `flame.svg`
- `perf-report.txt` (`perf report --stdio`)

Also append the single case row to `results.csv` as today.

## Commands

```bash
perf record -F "$PERF_FREQ" -g --call-graph "$PERF_CALL_GRAPH" \
  -p "$VGW_PID" -o "$dir/perf.data" -- sleep "$STRESS_SECONDS"

perf script -i "$dir/perf.data" > "$dir/perf.script"
"$FLAMEGRAPH_DIR/stackcollapse-perf.pl" "$dir/perf.script" \
  | "$FLAMEGRAPH_DIR/flamegraph.pl" --title "vgw $mode $case" > "$dir/flame.svg"
perf report -i "$dir/perf.data" --stdio --no-children > "$dir/perf-report.txt"
```

## Code changes

| Path | Change |
|------|--------|
| `tools/pktgen/env.sh` | Defaults for profile/perf env |
| `tools/pktgen/perf_profile.sh` | Helper: wait slack → record → collapse → report |
| `tools/pktgen/run.sh` | `PROFILE=1` path; invoke helper around `run_one_case` |
| `tools/pktgen/README.md` | Usage, deps (`linux-tools-*`, clone FlameGraph), RelWithDebInfo note |

## Non-goals

- Auto-profile every matrix case  
- speedscope / continuous profiling  
- Sampling pktgen or system-wide `-a`  
- Source instrumentation in `vgw`  
- Auto-tuning from flame graphs  

## Success criteria

```bash
sudo PROFILE=1 STRESS_DATAPATH_MODES=rtc PROFILE_CASE=M01 ./tools/pktgen/run.sh
```

Produces a readable `out/perf/rtc_M01/flame.svg` and a CSV row for that case.
