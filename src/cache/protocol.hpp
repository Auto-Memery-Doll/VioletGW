#ifndef FLOW_GATEWAY_PROTOCOL_HPP
#define FLOW_GATEWAY_PROTOCOL_HPP

#include "base/noncopyable.hpp"
#include <cstdint>
#include <ctime>
#include <sys/types.h>
namespace fg {
namespace cache {

// fg cache file header
// 只能用来将存储在文件系统中的文件的内容妆化为Protocol
class Protocol {
public:
    Protocol() = delete;
    ~Protocol() = delete;
    DISABLE_MOVE(Protocol);
    DISABLE_COPY(Protocol);

    /** 判断读取的文件是不是.fg文件 */
    inline bool check() {
        static u_char stander_header[] = "fg cache file header\n";
        for (int i = 0; i < 22; ++ i) {
            if (stander_header[i] != _header[i]) {
                return false;
            }
        }
        return true;
    }

private:
    u_char _header[22];  /** 协议头，分辨该文件是否fg 缓存文件 */
    uint32_t _len;       /** 内容的长度 */
    char _context[0];    /** 动态数组，储存剩下的文件的信息 */
};

using CacheBlock = Protocol;
inline CacheBlock* make_cache_block(char *block) {
    return reinterpret_cast<CacheBlock*>(block);
}

/** cache中节点的状态 */
enum class CacheNodeStatus {
    none,   /** 文件在最近的时间内没有被打开过 */
    once,   /** 文件在最近的时间内已经被打开了一次 */
    opened, /** 文件已经处于打开状态 */
};

//
//
// trie中保存的元素的结构
struct CacheEle {
    int fd; /** 文件描述符符 */
    CacheNodeStatus status = CacheNodeStatus::none;   /** 是否在lru2管理器中打开 */
};

}   // cache
}   // fg

#endif // !FLOW_GATEWAY_PROTOCOL_HPP