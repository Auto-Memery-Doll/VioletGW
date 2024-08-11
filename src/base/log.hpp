#ifndef FLOW_GATEWAY_LOG_HPP
#define FLOW_GATEWAY_LOG_HPP


#include "base/singleton.hpp"
#include <iostream>
namespace fg {

class consule_logger : public base::Singletion<consule_logger> {
    friend class base::Singletion<consule_logger>;
public:

    class Info {
    public:
        template<typename Param>
        Info& operator<<(Param&& p) {
            std::cout << p << std::endl;
        }
    };
    class Debug {
        template<typename Param>
        Info& operator<<(Param&& p) {
            std::cout << p << std::endl;
        }
    };
    class Warnning {
        template<typename Param>
        Info& operator<<(Param&& p) {
            std::cout << p << std::endl;
        }
    };
    class Error {
        template<typename Param>
        Info& operator<<(Param&& p) {
            std::cout << p << std::endl;
        }
    };

    Info& type_consule_logger_info() { return _info; }
    Debug& type_consule_logger_debug() { return _debug; }
    Warnning& type_consule_logger_warnning() { return _warnning; }
    Error& type_consule_logger_error() { return _error; }

private:
    Info _info;
    Debug _debug;
    Warnning _warnning;
    Error _error;
};

}   // fg


#define LOG(type, level)  type ## _logger_ ## level

#endif // !FLOW_GATEWAY_LOG_HPP