#pragma once

#include <cstdint>

namespace vgw {
namespace config {

/********************* L4 forward / lab seed (dev defaults) ***********/
/** VIP listened by the gateway (octets → network-order via VioletGW::ipv4_from) */
constexpr uint8_t VIP_IP_OCTETS[4] = {192, 168, 1, 100};
constexpr uint16_t VIP_PORT = 53;

constexpr uint8_t GATEWAY_IP_OCTETS[4] = {192, 168, 1, 10};

/** Placeholder MACs for L2 rewrite until control plane / neighbor discovery */
constexpr uint8_t GATEWAY_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
/** Lab: client NIC is also the upstream endpoint (same next-hop MAC). */
constexpr uint8_t UPSTREAM_NH_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x03};
constexpr uint8_t CLIENT_NH_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x03};

/** Initial upstream list (control plane will replace via UpstreamTable::set) */
constexpr uint8_t UPSTREAM0_IP_OCTETS[4] = {10, 0, 0, 1};
constexpr uint16_t UPSTREAM0_PORT = 53;

constexpr uint64_t SESSION_IDLE_TIMEOUT_MS = 60'000;
constexpr uint64_t SESSION_EXPIRE_INTERVAL_MS = 1'000;

/** Default balance policy for new flows (atomic publish later from CP) */
constexpr uint8_t BALANCE_POLICY = 0;  // 0 = mod, 1 = rr

/** POSIX SHM name for control-plane config block */
constexpr char CP_SHM_NAME[] = "/vgw_cp";
constexpr uint64_t CP_POLL_INTERVAL_MS = 1'000;

constexpr uint64_t ARP_ENTRY_TIMEOUT_MS = 300'000;
constexpr uint64_t ARP_EXPIRE_INTERVAL_MS = 60'000;
constexpr uint64_t ARP_PROBE_INTERVAL_MS = 1'000;
constexpr uint64_t ARP_PENDING_TIMEOUT_MS = 5'000;
constexpr unsigned ARP_PENDING_MAX_PER_IP = 64;

}  // namespace config
}  // namespace vgw
