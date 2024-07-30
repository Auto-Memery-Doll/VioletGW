#include <stdlib.h>
#include <rte_common.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_log.h>
#include <rte_mbuf.h>
#include <rte_mbuf_core.h>
#include <rte_mempool.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

struct rte_mempool * g_mp;
#define BUFS_NUM    1024    /* 内存块的大小 */
#define BUFS_SIZE   32      /* 内存块的数量 */

void inti(int argc, char ** argv, bool is_init) {
    if (is_init && rte_eal_init(argc, argv) < 0) {
        rte_exit(EXIT_FAILURE, "rte_eal_init() failure.\n");
    }

    struct rte_mempool *pool = rte_pktmbuf_pool_create(
        "tempool", 
        BUFS_NUM, 
        0, 0, 
        RTE_MBUF_DEFAULT_BUF_SIZE, 
        rte_socket_id());
    if (pool == NULL) {
        rte_exit(EXIT_FAILURE, "create mempool failure.\n");
    }

    g_mp = pool;

    printf("init success\n");
}

void test() {

    struct rte_mbuf *buf = rte_pktmbuf_alloc(g_mp);
    
    rte_pktmbuf_attach(buf, buf);
    rte_pktmbuf_clone(buf, g_mp);
    
}

int main(int argc, char ** argv) {

}