#!/usr/bin/env bash
# Bind / unbind stress NICs with vfio-pci (ens33 protected).
#
#   sudo ./setup_nics.sh status|bind|bind-sut|unbind
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
    echo "bind: ${DPDK_BIND_ARGS}  (keep ${MGMT_IFACE} on kernel)" >&2
    # shellcheck disable=SC2086
    run_tool setup ${DPDK_BIND_ARGS}
    ;;
  bind-sut)
    echo "bind-sut: ${SUT_PCI} only (${SUT_IFACE}=vgw; kernel client on ${KERNEL_CLIENT_IFACE})" >&2
    run_tool setup "${SUT_PCI}"
    ;;
  unbind)
    run_tool unbind
    ;;
  *)
    echo "Usage: $0 status|bind|bind-sut|unbind" >&2
    exit 1
    ;;
esac
