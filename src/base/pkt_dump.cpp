#include "base/pkt_dump.hpp"

#include "base/util.hpp"

#include <cctype>
#include <cstdio>
#include <netinet/in.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <sstream>

namespace vgw {
namespace util {
namespace {

constexpr uint16_t kEthHdrLen = static_cast<uint16_t>(sizeof(rte_ether_hdr));
constexpr uint16_t kMinIpHdrLen = static_cast<uint16_t>(sizeof(rte_ipv4_hdr));
constexpr uint16_t kUdpHdrLen = static_cast<uint16_t>(sizeof(rte_udp_hdr));

void append_mac(std::ostringstream& os, const rte_ether_addr& addr) {
    char buf[VGW_MAC_DUMP_LEN];
    mac_dump(buf, addr);
    os << buf;
}

void append_ipv4(std::ostringstream& os, uint32_t be_addr) {
    const uint32_t host = rte_be_to_cpu_32(be_addr);
    os << ((host >> 24) & 0xff) << '.' << ((host >> 16) & 0xff) << '.'
       << ((host >> 8) & 0xff) << '.' << (host & 0xff);
}

void append_ascii_preview(std::ostringstream& os, const uint8_t* data,
                          size_t n) {
    os << '"';
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = data[i];
        os << (std::isprint(c) != 0 ? static_cast<char>(c) : '.');
    }
    os << '"';
}

void append_hex_preview(std::ostringstream& os, const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (i != 0) {
            os << (i % 16 == 0 ? "\n             " : " ");
        }
        char byte[4];
        std::snprintf(byte, sizeof(byte), "%02x", data[i]);
        os << byte;
    }
}

}  // namespace

std::string dump_udp_mbuf(const rte_mbuf* m, size_t max_payload) {
    std::ostringstream os;
    if (m == nullptr) {
        return "UDP dump: <null mbuf>";
    }

    const uint16_t data_len = rte_pktmbuf_data_len(m);
    os << "=== UDP packet ===\n";
    os << "  mbuf  data_len=" << data_len << " pkt_len=" << m->pkt_len
       << " nb_segs=" << static_cast<unsigned>(m->nb_segs) << '\n';

    if (data_len < kEthHdrLen + kMinIpHdrLen + kUdpHdrLen) {
        os << "  error: too short for Eth/IPv4/UDP headers\n";
        return os.str();
    }

    const auto* eth = rte_pktmbuf_mtod(m, const rte_ether_hdr*);
    const uint16_t ethertype = rte_be_to_cpu_16(eth->ether_type);
    os << "  L2    src=";
    append_mac(os, eth->src_addr);
    os << "  dst=";
    append_mac(os, eth->dst_addr);
    os << "  ethertype=";
    if (ethertype == RTE_ETHER_TYPE_IPV4) {
        os << "IPv4(0x0800)";
    } else {
        char et[16];
        std::snprintf(et, sizeof(et), "0x%04x", ethertype);
        os << et;
        os << "\n  error: not IPv4\n";
        return os.str();
    }
    os << '\n';

    const auto* ip = reinterpret_cast<const rte_ipv4_hdr*>(
        rte_pktmbuf_mtod_offset(m, const uint8_t*, kEthHdrLen));
    const uint16_t ihl =
        static_cast<uint16_t>((ip->version_ihl & 0x0f) * 4u);
    if ((ip->version_ihl >> 4) != 4 || ihl < kMinIpHdrLen) {
        os << "  error: bad IPv4 header\n";
        return os.str();
    }
    if (data_len < kEthHdrLen + ihl + kUdpHdrLen) {
        os << "  error: truncated before UDP header\n";
        return os.str();
    }

    os << "  L3    src=";
    append_ipv4(os, ip->src_addr);
    os << "  dst=";
    append_ipv4(os, ip->dst_addr);
    os << "  ttl=" << static_cast<unsigned>(ip->time_to_live)
       << "  ihl=" << ihl
       << "  total_len=" << rte_be_to_cpu_16(ip->total_length)
       << "  id=" << rte_be_to_cpu_16(ip->packet_id);
    {
        char ck[8];
        std::snprintf(ck, sizeof(ck), "0x%04x",
                      rte_be_to_cpu_16(ip->hdr_checksum));
        os << "  cksum=" << ck;
    }
    os << "  proto=" << static_cast<unsigned>(ip->next_proto_id);
    if (ip->next_proto_id != IPPROTO_UDP) {
        os << "\n  error: not UDP (proto="
           << static_cast<unsigned>(ip->next_proto_id) << ")\n";
        return os.str();
    }
    os << '\n';

    const auto* udp = reinterpret_cast<const rte_udp_hdr*>(
        rte_pktmbuf_mtod_offset(m, const uint8_t*, kEthHdrLen + ihl));
    const uint16_t udp_len = rte_be_to_cpu_16(udp->dgram_len);
    const uint16_t sport = rte_be_to_cpu_16(udp->src_port);
    const uint16_t dport = rte_be_to_cpu_16(udp->dst_port);
    os << "  L4    UDP  sport=" << sport << "  dport=" << dport
       << "  udp_len=" << udp_len;
    {
        char ck[8];
        std::snprintf(ck, sizeof(ck), "0x%04x",
                      rte_be_to_cpu_16(udp->dgram_cksum));
        os << "  cksum=" << ck << '\n';
    }

    if (udp_len < kUdpHdrLen) {
        os << "  error: UDP length < header\n";
        return os.str();
    }
    if (data_len < kEthHdrLen + ihl + udp_len) {
        os << "  error: truncated UDP payload\n";
        return os.str();
    }

    const uint16_t payload_len =
        static_cast<uint16_t>(udp_len - kUdpHdrLen);
    const auto* payload = reinterpret_cast<const uint8_t*>(udp) + kUdpHdrLen;
    os << "  data  payload_len=" << payload_len << '\n';

    if (payload_len == 0 || max_payload == 0) {
        return os.str();
    }

    const size_t show =
        payload_len < max_payload ? payload_len : max_payload;
    os << "  ascii ";
    append_ascii_preview(os, payload, show);
    if (show < payload_len) {
        os << "  (+" << (payload_len - show) << " bytes)";
    }
    os << '\n';
    os << "  hex   ";
    append_hex_preview(os, payload, show);
    if (show < payload_len) {
        os << " ...";
    }
    os << '\n';
    return os.str();
}

}  // namespace util
}  // namespace vgw
