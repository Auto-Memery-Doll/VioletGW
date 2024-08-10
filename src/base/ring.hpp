#ifndef FLOW_GATEWAY_RING_HPP
#define FLOW_GATEWAY_RING_HPP

#include "base/noncopyable.hpp"
#include <cstddef>
#include <cstdlib>
#include <linux/limits.h>
#include <memory>
#include <rte_common.h>
#include <rte_errno.h>
#include <rte_lcore.h>
#include <rte_ring.h>
#include <rte_ring_core.h>

namespace fg {

template<typename T>
class Ring : public base::noncopyable {
public:
    using ptr = std::shared_ptr<Ring<T>>;
    ~Ring() {
        rte_ring_free(_ring);
    }

    auto push_burst(T* eles[], unsigned int size) -> unsigned int {
        unsigned int nb_en =
            rte_ring_enqueue_burst(_ring, (void**)eles, 
            size, &_free_space);
        return nb_en;
    }

    auto pop_burst(T* eles[], size_t size) -> unsigned int {
        unsigned int nb_de = 
            rte_ring_dequeue_burst(_ring, (void**)eles, 
                size, &_free_space);
        return nb_de;
    }

    inline auto free_size() -> unsigned int {
        return _free_space;
    }

    
    Ring(struct rte_ring* ring, unsigned int count) 
        :   _ring(ring)
        ,   _free_space(count)
    {}

private:
    struct rte_ring *_ring;
    unsigned int _free_space;/**每次push之后需要更新 */
};

template<typename T>
typename Ring<T>::ptr make_ring(const char* name, unsigned int count, unsigned int flag) {
    struct rte_ring *ring = rte_ring_create(name, count, rte_socket_id(), flag);
    if (ring == NULL) {
        rte_exit(EXIT_FAILURE, "rte_ring_create() failure.\n"
                "what():%s\n", rte_strerror(rte_errno));
    }
    return std::make_shared<Ring<T>>(ring, count);
}

}

#endif // !FLOW_GATEWAY_RING_HPP