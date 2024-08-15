#ifndef FLOW_GATEWAY_STORAGE_ENGINE_HPP
#define FLOW_GATEWAY_STORAGE_ENGINE_HPP

#include "base/iobuf.hpp"
#include "base/singleton.hpp"
#include "base/util.hpp"
#include "base/log.hpp"
#include <cstddef>
#include <cstdio>
#include <string>
#include <sys/types.h>

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
    // 路径上的目录必须存在
    int create_file(const std::string& path);

    /** 创建|path|上设计到的所有目录 */
    // 路径上的所有名称都被视为将要创建的目录
    bool create_dir(const std::string& path);

    /** 移除操作 */
    // 移除一个文件
    bool remove_file(const std::string& path);
    // 移除一个空目录
    bool remove_dir(const std::string& path);
    // 递归移除一个目录
    bool remove_dir_all(const std::string& path);

    /** 打开文件 */
    int open(const std::string& path);

    /** 关闭 */
    int close(int fd);

    /** 文件io操作 */
    /** 将指定路径的下的文件中的内容读取到|buffer|中，并返回一个CacheBlock */
    size_t read(int fd, off_t offset, base::IOBuf* iobuf, size_t size);
    size_t read(const std::string& path, off_t offset, base::IOBuf* buf, size_t size);

    /** 将iobuf中的内容写入 将指定路径下的文件 */
    size_t write(int fd, base::IOBuf* iobuf);
    size_t write(const std::string& path, base::IOBuf* iobuf);

    /** 持久化一个数据结构到指定的路径下 */
    template<typename T, base::IOBuf(*save)(T*)>
    void save_structure(const std::string& path, T* t) {
        base::IOBuf iobuf = save(t);
        int fd = create_file(path);
        if (fd != -1) {
            LOG(consule, error) << "save structure failure\n";
            return;
        }
        if (iobuf.size() == iobuf.output_file(fd, 0)) {
            LOG(consule, warnning) << "iobuf output file failure\n";
        }
    }
};

inline auto storage_engine() -> StorageEngine::ptr {
    return StorageEngine::GetInstance();
}

}   // cache
}   // fg


#endif // !FLOW_GATEWAY_STORAGE_ENGINE_HPP