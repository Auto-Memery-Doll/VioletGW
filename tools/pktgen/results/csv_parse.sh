#!/usr/bin/env bash
# Key=value parser for PKTGEN_SUMMARY → results.csv rows.
# Large counters / rates are stored in mega-units with 2 decimal places.

# Format number / 1e6 with 2 decimal places (empty → empty).
to_mega() {
  local v="${1:-}"
  [[ -n "${v}" ]] || { printf ''; return; }
  python3 -c "print(f'{float('${v}') / 1e6:.2f}')"
}

append_csv_row() {
  local out="$1"
  local datapath_mode="$2"
  local line="$3"
  local pktgen_rate="${4:-}"
  local tok k v ts
  local loss_pct
  declare -A m=()
  for tok in ${line}; do
    [[ "${tok}" == *=* ]] || continue
    k="${tok%%=*}"
    v="${tok#*=}"
    m["${k}"]="${v}"
  done
  ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  loss_pct="$(python3 -c "print(f'{float('${m[loss_rate_pct]:-0}'):.2f}')")"
  # Units: *_M = mega packets; *_Mpps; *_Mbps (bits/s / 1e6)
  echo "${ts},${datapath_mode},${m[case]},${m[pattern]},${m[flows]},${m[payload]},${m[seconds]},${m[warmup]},${pktgen_rate},$(to_mega "${m[client_sent]}"),$(to_mega "${m[client_received]}"),$(to_mega "${m[lost]}"),${loss_pct},$(to_mega "${m[sent_pps]}"),$(to_mega "${m[received_pps]}"),$(to_mega "${m[offered_bps]}"),$(to_mega "${m[received_bps]}"),${m[frame_bytes]}" >>"${out}"
}
