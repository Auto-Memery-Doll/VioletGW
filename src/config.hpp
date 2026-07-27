#pragma once

#include <cstdint>

namespace vgw {
namespace config {

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
constexpr char CP_SHM_NAME[] = "/vgw_cp";
constexpr uint64_t CP_POLL_INTERVAL_MS = 1'000;

}  // namespace config
}  // namespace vgw
