#!/usr/bin/env bash
# Key=value parser for PKTGEN_SUMMARY → results.csv rows.
append_csv_row() {
  local out="$1"
  local datapath_mode="$2"
  local line="$3"
  local tok k v ts
  declare -A m=()
  for tok in ${line}; do
    [[ "${tok}" == *=* ]] || continue
    k="${tok%%=*}"
    v="${tok#*=}"
    m["${k}"]="${v}"
  done
  ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "${ts},${datapath_mode},${m[case]},${m[pattern]},${m[flows]},${m[payload]},${m[seconds]},${m[warmup]},${m[client_sent]},${m[client_received]},${m[lost]},${m[loss_rate_pct]},${m[sent_pps]},${m[received_pps]},${m[offered_bps]},${m[received_bps]},${m[frame_bytes]}" >>"${out}"
}
