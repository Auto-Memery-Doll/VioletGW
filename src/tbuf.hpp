#ifndef FLOW_GATEWAY_TBUF_HPP
#define FLOW_GATEWAY_TBUF_HPP

#include "lwip/pbuf.h"
#include <rte_mbuf_core.h>


class Tbuf {
public:
    static auto pbuf_to_mbuf(pbuf* p_buf) -> rte_mbuf*;
    static auto mbuf_to_pbuf(rte_mbuf* m_buf) -> pbuf*;

private:
    pbuf *_pbuf;
    rte_mbuf *_mbuf;
};

#endif // !FLOW_GATEWAY_TBUF_HPP

