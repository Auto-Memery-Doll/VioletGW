#ifndef FLOW_GATEWAY_STACK_HPP
#define FLOW_GATEWAY_STACK_HPP

#include "base/singleton.hpp"
#include "base/type.hpp"

namespace fg {

class ProtoStack : public Singletion<ProtoStack> {
    friend Singletion<ProtoStack>;
public:

private:
    fg_thread_t _t;

};

}   // fg


#endif // !FLOW_GATEWAY_STACK_HPP