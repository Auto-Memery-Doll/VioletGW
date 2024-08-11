#ifndef FLOW_GATEWAY_LOG_HPP
#define FLOW_GATEWAY_LOG_HPP

#include "base/util.hpp"
#include "base/singleton.hpp"
namespace fg {

class consule_logger : public base::Singletion<consule_logger> {
    friend class base::Singletion<consule_logger>;
public:
    FG_LOG_TEMPLATE(info);
    FG_LOG_TEMPLATE(debug);
    FG_LOG_TEMPLATE(warnning);
    FG_LOG_TEMPLATE(error);

    FG_LOG_FACTORY(consule, info) {
        _info << "[info]\n";
        return _info;
    }

    FG_LOG_FACTORY(consule, debug) {
        _debug << "[debug]";
        FG_LOG_FORMAT(debug);
        return _debug;
    }

    FG_LOG_FACTORY(consule, warnning) {
        _warnning << "[warnning]";
        FG_LOG_FORMAT(warnning);
        return _warnning;
    }

    FG_LOG_FACTORY(consule, error) {
        _error << "[error]";
        FG_LOG_FORMAT(error);
        return _error;
    }

private:
    info _info;
    debug _debug;
    warnning _warnning;
    error _error;
};

}   // fg

#endif // !FLOW_GATEWAY_LOG_HPP