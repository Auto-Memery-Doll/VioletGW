#!/usr/bin/env bash
# Unit test for PKTGEN_SUMMARY → CSV parsing (no sed \10+ backrefs).
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=csv_parse.sh
source "${DIR}/csv_parse.sh"

tmp="$(mktemp)"
trap 'rm -f "${tmp}"' EXIT

sample="PKTGEN_SUMMARY case=M01 pattern=hot payload=4 seconds=30 warmup=5 flows=1 client_sent=1000000 client_received=999000 lost=1000 loss_rate_pct=0.1000 sent_pps=33333 received_pps=33299 offered_bps=12222132 received_bps=12209672 frame_bytes=46"
append_csv_row "${tmp}" "pipeline" "${sample}"

row="$(tail -1 "${tmp}")"
IFS=, read -r _ts _mode case_id pattern flows payload seconds warmup \
  client_sent client_received lost loss_rate_pct sent_pps received_pps \
  offered_bps received_bps frame_bytes <<<"${row}"

assert_eq() {
  local name="$1" got="$2" want="$3"
  [[ "${got}" == "${want}" ]] || {
    printf 'FAIL %s: got=%q want=%q\n' "${name}" "${got}" "${want}" >&2
    exit 1
  }
}

assert_eq case_id "${case_id}" "M01"
assert_eq pattern "${pattern}" "hot"
assert_eq flows "${flows}" "1"
assert_eq payload "${payload}" "4"
assert_eq client_sent "${client_sent}" "1000000"
assert_eq client_received "${client_received}" "999000"
assert_eq lost "${lost}" "1000"
assert_eq loss_rate_pct "${loss_rate_pct}" "0.1000"
assert_eq sent_pps "${sent_pps}" "33333"
assert_eq received_pps "${received_pps}" "33299"
assert_eq offered_bps "${offered_bps}" "12222132"
assert_eq received_bps "${received_bps}" "12209672"
assert_eq frame_bytes "${frame_bytes}" "46"

printf 'OK csv_parse: all fields match\n'
