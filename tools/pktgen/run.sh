#!/usr/bin/env bash
# PCI stress: vgw (ens192) + pktgen client (ens256). Upstream = client (no Python echo).
#
#   sudo -E ./run.sh
#
# SSH must stay on ens33. VMware: put ens192/ens256 on the same (promiscuous) LAN.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "${DIR}/env.sh"
# shellcheck source=results/csv_parse.sh
source "${DIR}/results/csv_parse.sh"

OUT_DIR="${STRESS_OUT_DIR}"
LUA_DIR="${OUT_DIR}/lua"
VGW_PID=""
BOUND_BY_US=0
PERF_PID=""

log() { printf '[pktgen] %s\n' "$*" >&2; }
die() { printf '[pktgen][ERROR] %s\n' "$*" >&2; exit 1; }

# M01–M06 → pattern:flows:payload
case_spec() {
  case "$1" in
    M01) echo "hot:1:4" ;;
    M02) echo "hot:1:1400" ;;
    M03) echo "flows:1000:4" ;;
    M04) echo "flows:1000:1400" ;;
    M05) echo "flows:10000:4" ;;
    M06) echo "hot:1:64" ;;
    *) return 1 ;;
  esac
}

ensure_dpdk_idle() {
  local pids
  pids="$(pgrep -x vgw 2>/dev/null || true)"
  pids="${pids} $(pgrep -f '/app/pktgen' 2>/dev/null || true)"
  pids="$(echo "${pids}" | xargs)"
  [[ -z "${pids}" ]] && return 0
  die "DPDK already in use (pids: ${pids}). Stop manual vgw/pktgen first, e.g. sudo pkill vgw; sudo pkill -f app/pktgen"
}

[[ "${EUID}" -eq 0 ]] || die "need root, e.g. sudo -E $0"

# Offer tag for filenames (scale or absolute target Mpps).
pktgen_offer_tag() {
  if [[ -n "${PKTGEN_TARGET_MPPS}" ]]; then
    printf 't%s' "${PKTGEN_TARGET_MPPS}"
  else
    printf 's%s' "${PKTGEN_OFFER_SCALE}"
  fi
}

# Target offer Mpps: TARGET overrides; else BASELINE × SCALE; SCALE>=1 → full blast (no Mpps cap).
pktgen_target_mpps() {
  if [[ -n "${PKTGEN_TARGET_MPPS}" ]]; then
    printf '%s' "${PKTGEN_TARGET_MPPS}"
    return
  fi
  python3 -c "print(float('${PKTGEN_BASELINE_MPPS}') * float('${PKTGEN_OFFER_SCALE}'))"
}

# Effective pktgen rate % for a frame size.
# Full blast (scale>=1, no TARGET): PKTGEN_RATE (typically 100).
# Else: convert target Mpps → fractional % of line rate (pktgen accepts floats down to 0.01).
pktgen_rate_effective() {
  local frame_bytes="${1:?frame_bytes}"
  local scale target
  if [[ -z "${PKTGEN_TARGET_MPPS}" ]]; then
    scale="$(python3 -c "print(float('${PKTGEN_OFFER_SCALE}'))")"
    if python3 -c "raise SystemExit(0 if float('${scale}') >= 1.0 else 1)"; then
      python3 -c "print(max(0.01, min(100.0, float('${PKTGEN_RATE}'))))"
      return
    fi
  fi
  target="$(pktgen_target_mpps)"
  python3 "${DIR}/rate_pct.py" --target-mpps "${target}" --frame-bytes "${frame_bytes}" \
    --link-gbps "${PKTGEN_LINK_GBPS}"
}

# Filename-safe rate tag (may be fractional, e.g. 0.1904).
pktgen_rate_tag() {
  local frame_bytes="${1:?frame_bytes}"
  local rate
  rate="$(pktgen_rate_effective "${frame_bytes}")"
  PYTHONPATH="${DIR}${PYTHONPATH:+:${PYTHONPATH}}" \
    python3 -c "from rate_pct import format_rate_tag; print(format_rate_tag(float('${rate}')))"
}

# Unique results CSV path under OUT_DIR (mode / case / offer tag; timestamp if collision).
results_csv_path() {
  local -a modes=("$@")
  local stamp base path
  local modes_tag case_tag offer_tag
  offer_tag="$(pktgen_offer_tag)"
  modes_tag="$(IFS=+; echo "${modes[*]}")"
  modes_tag="${modes_tag// /}"
  if [[ "${PROFILE}" == "1" ]]; then
    case_tag="_${PROFILE_CASE_TAG:-${PROFILE_CASE}}"
  else
    case_tag="_matrix"
  fi
  base="results_${modes_tag}${case_tag}_${offer_tag}"
  path="${OUT_DIR}/${base}.csv"
  if [[ -e "${path}" ]]; then
    stamp="$(date -u +%Y%m%dT%H%M%SZ)"
    path="${OUT_DIR}/${base}_${stamp}.csv"
  fi
  printf '%s\n' "${path}"
}

# Unique perf output dir: out/perf/<mode>_<case>_<offer>[/_stamp]
perf_out_dir() {
  local mode="$1"
  local case_id="$2"
  local stamp base path offer_tag
  offer_tag="$(pktgen_offer_tag)"
  base="${mode}_${case_id}_${offer_tag}"
  path="${OUT_DIR}/perf/${base}"
  if [[ -e "${path}" ]]; then
    stamp="$(date -u +%Y%m%dT%H%M%SZ)"
    path="${OUT_DIR}/perf/${base}_${stamp}"
  fi
  printf '%s\n' "${path}"
}

stop_vgw() {
  if [[ -n "${VGW_PID}" ]]; then
    kill "${VGW_PID}" 2>/dev/null || true
    wait "${VGW_PID}" 2>/dev/null || true
    VGW_PID=""
  fi
}

stop_perf() {
  if [[ -n "${PERF_PID}" ]]; then
    kill "${PERF_PID}" 2>/dev/null || true
    wait "${PERF_PID}" 2>/dev/null || true
    PERF_PID=""
  fi
}

on_exit() {
  stop_perf
  stop_vgw
}
trap on_exit EXIT

maybe_bind() {
  if [[ "${STRESS_BIND}" != "1" ]]; then
    log "STRESS_BIND=0 — assuming ${DPDK_BIND_ARGS} already vfio-bound"
    return
  fi
  log "bind DPDK NICs: ${DPDK_BIND_ARGS} (mgmt=${MGMT_IFACE})"
  # shellcheck disable=SC2086
  MGMT_IFACE="${MGMT_IFACE}" "${DIR}/setup_nics.sh" bind
  BOUND_BY_US=1
}

publish_upstream() {
  [[ -x "${VGWCP_BIN}" ]] || die "vgwcp missing at ${VGWCP_BIN}"
  log "upstream=${UPSTREAM_IP}:${UPSTREAM_PORT} (client backend)"
  "${VGWCP_BIN}" -upstream "${UPSTREAM_IP}:${UPSTREAM_PORT}"
}

start_vgw() {
  local mode="$1"
  local lcores
  case "${mode}" in
    pipeline) lcores="${VGW_LCORES_PIPELINE}" ;;
    rtc) lcores="${VGW_LCORES_RTC}" ;;
    *) die "unknown datapath mode: ${mode}" ;;
  esac
  [[ -x "${VGW_BIN}" ]] || die "vgw missing; cmake --build build --target vgw"
  log "SUT=vgw datapath_mode=${mode} pci=${SUT_PCI} lcores=${lcores}"
  local -a cmd=(
    "${VGW_BIN}"
    --datapath_mode="${mode}"
    -l "${lcores}"
    --file-prefix=vgw
    -a "${SUT_PCI}"
  )
  if [[ "${mode}" == "rtc" ]]; then
    cmd+=(--rtc_workers=1)
  fi
  "${cmd[@]}" &
  VGW_PID=$!
  sleep 2
  kill -0 "${VGW_PID}" 2>/dev/null || die "vgw exited early (mode=${mode})"
  publish_upstream
}

run_one_case() {
  local datapath_mode="$1"
  local case_id="$2"
  local pattern="$3"
  local flows="$4"
  local payload="$5"
  local out="$6"
  local lcores="${PKTGEN_LCORES}"
  local map="${PKTGEN_MAP}"
  local prefix="pg_vgw_pci"
  local lua="${LUA_DIR}/${datapath_mode}_${case_id}.lua"
  local logf line pktgen_cmd rc timeout_sec
  local perf_dir=""
  local do_profile=0
  local rate_eff frame_bytes target_mpps offer_tag
  frame_bytes=$((14 + 20 + 8 + payload))
  rate_eff="$(pktgen_rate_effective "${frame_bytes}")"
  offer_tag="$(pktgen_offer_tag)"
  if [[ -n "${PKTGEN_TARGET_MPPS}" ]]; then
    target_mpps="${PKTGEN_TARGET_MPPS}"
  else
    target_mpps="$(pktgen_target_mpps)"
  fi

  log "case ${case_id} datapath=${datapath_mode} pattern=${pattern} flows=${flows} payload=${payload} rate=${rate_eff}% offer=${offer_tag} target_mpps=${target_mpps} (baseline_mpps=${PKTGEN_BASELINE_MPPS} link=${PKTGEN_LINK_GBPS}G)"
  python3 "${DIR}/gen_lua.py" --case "${case_id}" --pattern "${pattern}" \
    --payload "${payload}" --seconds "${STRESS_SECONDS}" --warmup "${STRESS_WARMUP}" \
    --flows "${flows}" --rate "${rate_eff}" \
    --src-ip "${CLIENT_IP}" --dst-ip "${VIP_IP}" --dst-port "${VIP_PORT}" \
    --src-mac "${CLIENT_MAC}" --dst-mac "${GW_MAC}" \
    -o "${lua}"

  mkdir -p "${OUT_DIR}/logs"
  logf="${OUT_DIR}/logs/${datapath_mode}_${case_id}.log"
  timeout_sec="${PKTGEN_TIMEOUT_SEC}"

  if [[ "${PROFILE}" == "1" ]]; then
    do_profile=1
    perf_dir="$(perf_out_dir "${datapath_mode}" "${case_id}")"
    mkdir -p "${perf_dir}"
    log "profile: starting perf helper → ${perf_dir}"
    "${DIR}/perf_profile.sh" "${VGW_PID}" "${perf_dir}" \
      "vgw ${datapath_mode} ${case_id} ${offer_tag}" &
    PERF_PID=$!
  fi

  pktgen_cmd=$(printf '%q ' timeout --foreground "${timeout_sec}" \
    "${PKTGEN_BIN}" -l "${lcores}" --file-prefix="${prefix}" \
    -a "${CLIENT_PCI}" \
    -- -P -m "${map}" -f "${lua}")
  log "pktgen -l ${lcores} -m ${map} (timeout ${timeout_sec}s)"
  set +e
  script -qefc "${pktgen_cmd}" /dev/null >"${logf}" 2>&1
  rc=$?
  set -e

  if [[ "${do_profile}" -eq 1 && -n "${PERF_PID}" ]]; then
    set +e
    wait "${PERF_PID}"
    local perf_rc=$?
    set -e
    PERF_PID=""
    if [[ "${perf_rc}" -ne 0 ]]; then
      log "perf_profile failed (rc=${perf_rc}); see ${perf_dir}"
      return 1
    fi
    log "flamegraph: ${perf_dir}/flame.svg"
  fi

  if [[ "${rc}" -eq 124 ]]; then
    log "pktgen timed out after ${timeout_sec}s for ${datapath_mode}/${case_id} (see ${logf})"
    return 1
  fi
  line="$(grep -o 'PKTGEN_SUMMARY.*' "${logf}" | tail -1 || true)"
  [[ -n "${line}" ]] || {
    log "missing PKTGEN_SUMMARY for ${datapath_mode}/${case_id} (see ${logf})"
    return 1
  }
  line="$(printf '%s' "${line}" | tr -d '\r' | sed 's/frame_bytes=\([0-9][0-9]*\).*/frame_bytes=\1/')"
  append_csv_row "${out}" "${datapath_mode}" "${line}" "${rate_eff}"
}

run_matrix() {
  local out=""
  local failures=0
  local modes=(${STRESS_DATAPATH_MODES})
  local cases=(
    "M01:hot:1:4"
    "M02:hot:1:1400"
    "M03:flows:1000:4"
    "M04:flows:1000:1400"
    "M05:flows:10000:4"
    "M06:hot:1:64"
  )
  local mode entry case_id pattern flows payload spec

  [[ -x "${PKTGEN_BIN}" ]] || die "pktgen missing at ${PKTGEN_BIN} (DEV_HOME=${DEV_HOME}); as violet: ${DIR}/build.sh"

  if [[ "${PROFILE}" == "1" ]]; then
    local -a profile_ids=()
    local id
    # PROFILE_CASE=all → M01–M06 in one run (one vgw per mode, flame per case).
    # PROFILE_CASES="M01 M03" overrides; else single PROFILE_CASE (default M01).
    if [[ -n "${PROFILE_CASES:-}" ]]; then
      # shellcheck disable=SC2206
      profile_ids=(${PROFILE_CASES})
    elif [[ "${PROFILE_CASE}" == "all" ]]; then
      profile_ids=(M01 M02 M03 M04 M05 M06)
    else
      profile_ids=("${PROFILE_CASE}")
    fi
    cases=()
    for id in "${profile_ids[@]}"; do
      spec="$(case_spec "${id}")" || die "PROFILE case must be M01–M06 (got: ${id})"
      IFS=: read -r pattern flows payload <<<"${spec}"
      cases+=("${id}:${pattern}:${flows}:${payload}")
    done
    if [[ "${#profile_ids[@]}" -eq 1 ]]; then
      export PROFILE_CASE_TAG="${profile_ids[0]}"
    else
      export PROFILE_CASE_TAG="all"
    fi
    log "PROFILE=1 modes=(${modes[*]}) cases=(${profile_ids[*]}) (warmup=${STRESS_WARMUP}s measure=${STRESS_SECONDS}s)"
  fi

  mkdir -p "${OUT_DIR}" "${LUA_DIR}"
  out="$(results_csv_path "${modes[@]}")"
  ensure_dpdk_idle
  maybe_bind

  echo "timestamp,datapath_mode,case_id,pattern,flows,payload_bytes,seconds,warmup,pktgen_rate,client_sent_M,client_received_M,lost_M,loss_rate_pct,sent_Mpps,received_Mpps,offered_Mbps,received_Mbps,frame_bytes" >"${out}"

  for mode in "${modes[@]}"; do
    start_vgw "${mode}"
    for entry in "${cases[@]}"; do
      IFS=: read -r case_id pattern flows payload <<<"${entry}"
      run_one_case "${mode}" "${case_id}" "${pattern}" "${flows}" "${payload}" "${out}" || failures=$((failures + 1))
    done
    stop_vgw
  done

  log "results: ${out}"
  if [[ "${PROFILE}" == "1" ]]; then
    log "flamegraphs under: ${OUT_DIR}/perf/<mode>_<case>_$(pktgen_offer_tag)/"
  fi
  if [[ "${failures}" -gt 0 ]]; then
    die "${failures} case(s) missing PKTGEN_SUMMARY"
  fi
  if [[ "${BOUND_BY_US}" -eq 1 ]]; then
    log "NICs still vfio-bound; restore with: ${DIR}/setup_nics.sh unbind"
  fi
}

run_matrix
