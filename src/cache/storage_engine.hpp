#ifndef FLOW_GATEWAY_STORAGE_ENGINE_HPP
#define FLOW_GATEWAY_STORAGE_ENGINE_HPP

#include "base/iobuf.hpp"
#include "base/singleton.hpp"
#include "cache/lru-2.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace fg {
namespace cache {

//
// [对象存储引擎]
// 负责将文件存储在linux的文件系统中
class StorageEngine : public base::Singletion<StorageEngine> {
    friend class base::Singletion<StorageEngine>;
public:
    using ptr = std::shared_ptr<StorageEngine>;

    /** 根据|path|创建一个文件以及对应的文件路径上的目录 */
    int create_file(const std::string& path);

    /** 创建|path|上设计到的所有目录 */
    int create_dir(const std::string& path);

    /** 移除操作 */
    int remove_file(const std::string& path);
    int remove_dir(const std::string& path);
    int remove_dir_all(const std::string& path);

    /** 文件io操作 */
    /** 将指定路径的下的文件中的内容读取到|buffer|中，并返回一个CacheBlock */
    size_t read(int fd, base::IOBuf* iobuf);
    size_t read(const std::string& path, base::IOBuf* buf);

    /** 将iobuf中的内容写入 将指定路径下的文件 */
    size_t write(int fd, base::IOBuf* iobuf);
    size_t write(const std::string& path, base::IOBuf* iobuf);

    /** 持久化一个数据结构到指定的路径下 */
    template<typename T, base::IOBuf(*save)(T*)>
    void save_structure(const std::string& path, T* t) {
        base::IOBuf iobuf = save(t);
        
    }

private:
    /** 打开一个文件并返回相应的文件描述符，同时进行lru-2算法 */
    // 防止打开过多的文件
    int open(const std::string& path);

    /** 关闭一个文件，如果该文件在文件池内，将其从池中移除 */
    int close(int fd);

    /** 如果热点数据过多，可以适当的扩充，动态适应 */
    static void adjust(size_t size);

    friend class LRU2<StorageEngine::adjust>;
private:
    std::vector<int> _fds; /** 记录已经打开的文件描述符 */
    LRU2<StorageEngine::adjust> _fd_manager;   /** 管理打开的文件描述符 */
};

inline auto storage_engine() -> StorageEngine::ptr {
    return StorageEngine::GetInstance();
}

}   // cache
}   // fg


#endif // !FLOW_GATEWAY_STORAGE_ENGINE_HPP