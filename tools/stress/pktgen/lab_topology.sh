#!/usr/bin/env bash
# Lab veth + addressing for pktgen unified bench (fg / nginx).
set -euo pipefail

# Interface names (override via env)
VETH_PG="${VETH_PG:-fg-pktgen0}"
VETH_SUT="${VETH_SUT:-fg-sut0}"

CLIENT_IP="${CLIENT_IP:-10.0.0.1/24}"
GW_IP="${GW_IP:-192.168.1.10/24}"
VIP_IP="${VIP_IP:-192.168.1.100/32}"
UPSTREAM_IP="${UPSTREAM_IP:-10.1.0.2/32}"

action="${1:-up}"

down() {
  ip link del "${VETH_PG}" 2>/dev/null || true
}

up() {
  down
  ip link add "${VETH_PG}" type veth peer name "${VETH_SUT}"
  ip addr add "${CLIENT_IP}" dev "${VETH_PG}"
  ip addr add "${GW_IP}" dev "${VETH_SUT}"
  ip addr add "${VIP_IP}" dev "${VETH_SUT}"
  ip addr add "${UPSTREAM_IP}" dev "${VETH_SUT}"
  ip link set "${VETH_PG}" up
  ip link set "${VETH_SUT}" up

  local pg_mac sut_mac
  pg_mac="$(ip link show "${VETH_PG}" | awk '/link\/ether/ {print $2; exit}')"
  sut_mac="$(ip link show "${VETH_SUT}" | awk '/link\/ether/ {print $2; exit}')"
  if [[ -z "${pg_mac}" || -z "${sut_mac}" ]]; then
    echo "failed to read MAC for ${VETH_PG} / ${VETH_SUT}" >&2
    exit 1
  fi

  # Static L2 reachability for pktgen (no ARP on DPDK tap path).
  ip neigh replace 192.168.1.100 lladdr "${sut_mac}" dev "${VETH_PG}" nud permanent
  ip neigh replace 10.0.0.1 lladdr "${pg_mac}" dev "${VETH_SUT}" nud permanent

  echo "Lab topology up:"
  echo "  ${VETH_PG} ${CLIENT_IP} mac=${pg_mac}"
  echo "  ${VETH_SUT} ${GW_IP} ${VIP_IP} ${UPSTREAM_IP} mac=${sut_mac}"
}

case "${action}" in
  up) up ;;
  down) down ;;
  *)
    echo "Usage: $0 up|down" >&2
    exit 1
    ;;
esac
