#!/usr/bin/env bash
# Bind / unbind stress NICs with vfio-pci (ens33 protected).
#
#   sudo ./setup_nics.sh status|bind|unbind
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "${DIR}/env.sh"

NIC_TOOL="${ROOT}/scripts/setup_dpdk_nics.sh"
[[ -x "${NIC_TOOL}" ]] || { echo "missing ${NIC_TOOL}" >&2; exit 1; }

ACTION="${1:-status}"
export MGMT_IFACE

run_tool() {
  if [[ "${EUID}" -eq 0 ]]; then
    MGMT_IFACE="${MGMT_IFACE}" "${NIC_TOOL}" "$@"
  else
    sudo -E MGMT_IFACE="${MGMT_IFACE}" "${NIC_TOOL}" "$@"
  fi
}

case "${ACTION}" in
  status)
    run_tool status
    ;;
  bind)
    echo "bind: ${DPDK_BIND_IFACES}  (keep ${MGMT_IFACE} + ${UPSTREAM_IFACE} on kernel)" >&2
    # shellcheck disable=SC2086
    run_tool setup ${DPDK_BIND_IFACES}
    ;;
  unbind)
    run_tool unbind
    ;;
  *)
    echo "Usage: $0 status|bind|unbind" >&2
    exit 1
    ;;
esac
