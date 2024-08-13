#ifndef FLOW_GATEWAY_FILE_WRITER_HPP
#define FLOW_GATEWAY_FILE_WRITER_HPP


#include "base/noncopyable.hpp"
#include "base/type.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <vector>
namespace fg {

class FileWriter : public base::noncopyable {

public:
    using ptr = std::unique_ptr<FileWriter>;
    FileWriter() = default;
    FileWriter(const std::string& name);
    ~FileWriter();

    void append(const char* cstr, size_t size);
    void append(const std::string& str);
    void append(char* cstr);
    void append(int);
    void append(double);
    void append(uint32_t);
    void append(uint64_t);

    void init(const std::string& name);
    void init();

    void join();

private:
    void worker();

    /** 控制块 */
    struct FilePcb : public base::noncopyable{
        FilePcb(char *buf) 
        :   len(0)
        {
            block = std::shared_ptr<char>(buf, [](char* block) {
                delete [] block;
            });
            ::memset(buf, 0, 1024);
        }

        FilePcb(FilePcb&& fp) {
            block = fp.block;
            len = fp.len;
            done = std::move(fp.done);

            fp.block.reset();
        }

        ~FilePcb() {}
        std::shared_ptr<char> block;
        size_t len;
        std::future<bool> done;
    };

    struct FileWritePcb : public base::noncopyable{
        FileWritePcb(const FilePcb& fpcb) 
        :   origin_len(0)
        ,   len(fpcb.len)
        ,   block(fpcb.block.get())
        {}

        FileWritePcb(FileWritePcb&& fwp) {
            block = fwp.block;
            len = fwp.len;
            origin_len = fwp.origin_len;
            p = std::move(fwp.p);

            fwp.block = 0;
        }

        ~FileWritePcb() {}
        char *block;
        size_t len;
        size_t origin_len;
        std::promise<bool> p;
    };

private:
    std::vector<FilePcb> _buf;   /** 多缓冲buf */
    std::FILE *_file;   /** 文件指针 */
    std::string _name;  /** 打开的文件的名字 */
    fg_thread_t _t; /** 后台线程 */
    std::condition_variable _cond;  /** 条件变量 */
    std::mutex _mtx;    /** 互斥锁保护缓冲区 */
    bool _stop; /** 日志是否停止 */
    int _timeval;   /** 刷盘的时间间隔 */
};

}

#endif // !FLOW_GATEWAY_FILE_WRITER_HPP