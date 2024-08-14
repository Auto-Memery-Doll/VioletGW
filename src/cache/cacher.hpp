#ifndef FLOW_GATEWAY_CACHER_HPP
#define FLOW_GATEWAY_CACHER_HPP

#include "base/iobuf.hpp"
#include "base/singleton.hpp"
#include "base/trie.hpp"
#include "cache/storage_engine.hpp"
#include <set>
#include <string>

namespace fg {
namespace cache {

class Cacher : public base::Singletion<Cacher> {
    friend class base::Singletion<Cacher>;
public:
    using Key = std::string;
    using Value = std::string;
    using FilePath = std::string;

    /** 缓存一个key */
    void cache(const Key& key, base::IOBuf* iobuf);

    /** 检查一个key是否被缓存 */
    // 如果是则直接从文件系统中读取，并将文件的数据存储到CacheRet中
    // 否则想上游服务器发起http请求
    int check_and_get(const Key& key, base::IOBuf* iobuf);

    /** 使一个key过期 */
    // 缓存是为了http为设计的，同时http中有相应的缓存机制
    // 即强制缓存和协商缓存，
    // 根据http协议来决定缓存是否过期
    void expire(const Key& key);

private:
    /** 计算出字符在字典中对应的哈希值 */
    static int hash(char);

    /** 将key转化为相应的文件路径 */
    FilePath parse(const Key& key);

private:
    base::Trie<bool, 26, Cacher::hash> _trie;    /** 用于缓存中小key */
    std::set<std::string> _set; /** 用于缓存大key */

    StorageEngine::ptr _engine; /** 存储引擎 */
};


}   // cache
}   // fg

#endif // !FLOW_GATEWAY_CACHER_HPP