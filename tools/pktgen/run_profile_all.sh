#!/usr/bin/env bash
# One-shot PROFILE=1 sweep: all cases (M01–M06) × datapath mode(s).
#
#   sudo PKTGEN_OFFER_SCALE=0.1 ./tools/pktgen/run_profile_all.sh
#
# Defaults: PROFILE_CASE=all, STRESS_DATAPATH_MODES=pipeline.
# Both modes + all cases:
#   sudo STRESS_DATAPATH_MODES="pipeline rtc" PKTGEN_OFFER_SCALE=0.1 \
#     ./tools/pktgen/run_profile_all.sh
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"

export PROFILE=1
export PROFILE_CASE="${PROFILE_CASE:-all}"
export STRESS_DATAPATH_MODES="${STRESS_DATAPATH_MODES:-pipeline}"

exec "${DIR}/run.sh"
