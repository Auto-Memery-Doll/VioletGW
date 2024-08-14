#ifndef FLOW_GATEWAY_PROTOCOL_HPP
#define FLOW_GATEWAY_PROTOCOL_HPP

#include "base/noncopyable.hpp"
#include "base/type.hpp"
#include "base/util.hpp"
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
    uint32_t _index;    /** 用于处理哈希碰撞 */
    char _context[0];    /** 动态数组，储存剩下的文件的信息 */
};

using CacheBlock = Protocol;
inline CacheBlock* make_cache_block(char *block) {
    return reinterpret_cast<CacheBlock*>(block);
}

//
//
// trie中保存的元素的结构
struct CacheEle {
    int fd; /** 文件描述符符 */
    bool lru2;   /** 是否在lru2管理器中打开 */
};

}   // cache
}   // fg

#endif // !FLOW_GATEWAY_PROTOCOL_HPP