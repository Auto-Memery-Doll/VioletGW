#include <rte_common.h>
#include <rte_mbuf_core.h>
#include <rte_mempool.h>
#include <rte_dmadev.h>
#include <rte_ethdev.h>
#include <rte_malloc.h>

void test() {
    //rte_dma_copy(int16_t dev_id, uint16_t vchan, rte_iova_t src, rte_iova_t dst, uint32_t length, uint64_t flags)
    //rte_dma_submit(int16_t dev_id, uint16_t vchan)
    #define DMA_BURST_SZ 64
    #define COPY_LEN 10
    struct rte_mbuf *srcs[DMA_BURST_SZ], *dsts[DMA_BURST_SZ];
    unsigned int i;

    int64_t dev_id, vchan;

    for (i = 0; i < RTE_DIM(srcs); ++ i) {
        if (rte_dma_copy(dev_id, vchan, rte_pktmbuf_iova(srcs[i]), 
            rte_pktmbuf_iova(dsts[i]), COPY_LEN, 0) < 0) {
            
            return;
        }
    } 

    // 触发dma硬件开始执行dma在enqueue中的操作
    rte_dma_submit(dev_id, vchan);

    //rte_dma_completed(int16_t dev_id, uint16_t vchan, const uint16_t nb_cpls, uint16_t *last_idx, bool *has_error)
    //rte_dma_completed_status(int16_t dev_id, uint16_t vchan, const uint16_t nb_cpls, uint16_t *last_idx, enum rte_dma_status_code *status)
    #undef COPY_LEN
    #undef DMA_BURST_SZ
}

int main(int argc, const char** argv) {
    return 0;
}