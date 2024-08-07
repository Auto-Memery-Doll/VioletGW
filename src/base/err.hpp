#ifndef FLOW_GATEWAY_ERR_HPP
#define FLOW_GATEWAY_ERR_HPP

#include "lwip/err.h"
#include <cerrno>
namespace fg {

/** 相关的错误码 */
enum class FG_ERR { 

    /** lwip */
    OK          = ERR_OK,           // ok
    MEM         = ERR_MEM,          // out of memory
    BUF         = ERR_BUF,          // buffer error
    TIMEOUT     = ERR_TIMEOUT,      // timeout
    RTE         = ERR_RTE,          // routing problem
    INPROGRESS  = ERR_INPROGRESS,   // operation in progress
    VAL         = ERR_VAL,          // illegal value
    WOULDBLOCK  = ERR_WOULDBLOCK,   // operation would block
    USE         = ERR_USE,          // address in use
    ALREADY     = ERR_ALREADY,      // already connecting
    ISCONN      = ERR_ISCONN,       // conn already established
    CONN        = ERR_CONN,         // not connected
    IF          = ERR_IF,           // low-level netif error
    ABRT        = ERR_ABRT,         // connection aborted
    RST         = ERR_RST,          // connection reset
    CLSD        = ERR_CLSD,         // connectin closed
    ARG         = ERR_ARG,          // illegal argument

    /** posix api */

};


}   // fg

#endif // !FLOW_GATEWAY_ERR_HPP