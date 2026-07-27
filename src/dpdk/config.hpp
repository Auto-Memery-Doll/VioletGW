#pragma once

#include <cstdint>
#include <rte_ether.h>
#include <rte_mbuf_core.h>

namespace vgw {
namespace config {

/********************* DPDK base configuration ******************/
/** dpdk mempool name */
const char DPDK_mempool_name[] = "vgw_dpdk_mempool";

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

/********************* Soft rings between NIC lcores and worker ********/
/** Burst size for NIC RX / TX on I/O lcores */
constexpr unsigned IO_RX_BURST = 32;
constexpr unsigned IO_TX_BURST = 32;

/** Capacity of per-port RX/TX rte_ring (power of two preferred by DPDK) */
constexpr uint16_t IO_RING_SIZE = 32;

/** rte_ring create flags (0 = default SP/SC as chosen by make_ring) */
constexpr uint16_t IO_RING_FLAGS = 0;

/** usleep when worker enqueue to TX ring fails (full) */
constexpr int IO_TX_RING_FULL_SLEEP_US = 10;

/** How many ports share one RX+TX lcore pair before allocating the next */
constexpr int IO_PORTS_PER_RXTX_LCORE_PAIR = 2;

}  // namespace config
}  // namespace vgw
