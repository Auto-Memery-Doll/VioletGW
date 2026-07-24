#pragma once

#include <cstdint>
#include <rte_ether.h>
#include <rte_mbuf_core.h>

namespace vgm {
namespace config {

/********************* DPDK base configuration ******************/
/** dpdk mempool name */
const char DPDK_mempool_name[] = "flow_gateway_dpdk_mempool";

/** mempool element size / count — verify against DPDK docs before tuning */
const unsigned DPDK_mempool_block_size = 1024;
const uint16_t DPDK_mempool_block_num = RTE_MBUF_DEFAULT_BUF_SIZE;

const unsigned DPDK_mempool_cache_size = 0;
const unsigned DPDK_mempool_private_size = 0;

/** LRO max size */
const uint32_t DPDK_port_rxmode_max_lro_size = RTE_ETHER_MAX_LEN;

const uint16_t DPDK_nb_rx_queue_desc = 128;
const uint16_t DPDK_nb_tx_queue_desc = 128;

/** enabled port bitmask (default: port 0) */
const uint32_t DPDK_vaild_port_marks = 0b0000'0000'0001;

const bool DPDK_tx_config_default = true;
const bool DPDK_rx_config_default = true;

const uint16_t DPDK_tx_queue_num = 1;
const uint16_t DPDK_rx_queue_num = 1;

const uint32_t DPDK_max_frame_size = 1024;

/********************* DPDK vdevice / ring configuration ***************/
const int VDEV_rx_burst_num = 32;
const int VDEV_tx_burst_num = 32;

const uint16_t VDEV_tx_ring_num = 32;
const uint16_t VDEV_rx_ring_num = 32;

const uint16_t VDEV_tx_ring_mode = 0;
const uint16_t VDEV_rx_ring_mode = 0;

/** sleep when TX ring full / RX ring empty (us) */
const int VDEV_tx_sleep = 10;
const int VDEV_rx_sleep = 10;

const int VDEV_core_max_rxtx = 2;

/********************* L4 forward / lab seed (dev defaults) ***********/
/** VIP listened by the gateway (IPv4 host order octets → use RTE_IPV4 in code) */
constexpr uint8_t VIP_IP_OCTETS[4] = {192, 168, 1, 100};
constexpr uint16_t VIP_PORT = 53;

constexpr uint8_t GATEWAY_IP_OCTETS[4] = {192, 168, 1, 10};

/** Placeholder MACs for L2 rewrite until control plane / neighbor discovery */
constexpr uint8_t GATEWAY_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
constexpr uint8_t UPSTREAM_NH_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
constexpr uint8_t CLIENT_NH_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x03};

/** Initial upstream list (control plane will replace via UpstreamTable::set) */
constexpr uint8_t UPSTREAM0_IP_OCTETS[4] = {10, 1, 0, 2};
constexpr uint16_t UPSTREAM0_PORT = 53;

constexpr uint64_t SESSION_IDLE_TIMEOUT_MS = 60'000;
constexpr uint64_t SESSION_EXPIRE_INTERVAL_MS = 1'000;

/** Default balance policy for new flows (atomic publish later from CP) */
constexpr uint8_t BALANCE_POLICY = 0;  // 0 = mod, 1 = rr

/** POSIX SHM name for control-plane config block */
constexpr char CP_SHM_NAME[] = "/flow_gateway_cp";
constexpr uint64_t CP_POLL_INTERVAL_MS = 1'000;

}  // namespace config
}  // namespace vgm

