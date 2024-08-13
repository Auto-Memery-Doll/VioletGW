#ifndef FLOW_GATEWAY_LOAD_BALANCE_HPP
#define FLOW_GATEWAY_LOAD_BALANCE_HPP

#include "base/singleton.hpp"
#include <memory>

namespace fg {


class LoadBalance : public base::Singletion<LoadBalance> {
    friend class base::Singletion<LoadBalance>;
public:
    using ptr = std::shared_ptr<LoadBalance>;




};

}   // fg

#endif // !FLOW_GATEWAY_LOAD_BALANCE_HPP