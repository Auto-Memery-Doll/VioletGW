#include "file_writer.hpp"
#include "base/log.hpp"
#include "base/util.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <future>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fg {

FileWriter::FileWriter(const std::string& name)
:   _name(name), _file(NULL), _stop(true), _timeval(1)
{}

FileWriter::~FileWriter() {
    _stop = true;
    _cond.notify_all();

    if (_t.joinable()) {
        _t.join();
    }

    if (_file) {
        fclose(_file);
    }
}

void FileWriter::init() {

    if ((_file = ::fopen(_name.c_str(), "a+")) == NULL) {
        LOG(consule, error) << "fopen() fail\n"
            << "what():" << ::strerror(errno) << "\n";
        return;
    }
    LOG(consule, info) << "create file " << _name << "\n";
    _stop = false;

    printf("%p\n", this);
    _t = std::thread([this](){
        worker();
    });
}



void FileWriter::init(const std::string& name) {
    _name = name;
    init();
}

void FileWriter::join() {
    _stop = true;
    _cond.notify_all();

    if (_t.joinable()) {
        _t.join();
    }

    if (_file) {
        fclose(_file);
        _file = nullptr;
    }
}

void FileWriter::worker() {
     printf("%p\n", this);

    while (!_stop || !_buf.empty()) {
        std::vector<FilePcb> flush_buf;
        
        std::unique_lock<std::mutex> lock(_mtx);
        /** 等待buf不为空，或者超时 */
        if (!_stop) {
            _cond.wait_for(lock, std::chrono::seconds(_timeval), 
                [&]{
                    return _stop || !flush_buf.empty();
                });
        }

        flush_buf.swap(_buf);
        lock.unlock();

        size_t cnt = 0;
        for (int i = 0; i < flush_buf.size(); ++ i) {
            flush_buf[i].done.get();    // 等待生产者生产
            int ret = ::fwrite_unlocked(flush_buf[i].block.get(), 1, 
                                        flush_buf[i].len, _file);
            assert(ret == flush_buf[i].len);
            cnt += ret;
        }
        //LOG(consule, info) << "write bytes " << cnt << "\n";
    }
}

void FileWriter::append(const char * ctx, size_t size) {
    std::vector<FileWritePcb> will_write;    /** 将要写入的pcb */

    std::unique_lock<std::mutex> lock(_mtx);
    
    size_t remain_bytes = 0;
    if (!_buf.empty()) {
        will_write.emplace_back(_buf.back());
        remain_bytes = 1024 - _buf.back().len;
        will_write.front().origin_len = _buf.back().len;
    }

    if (remain_bytes < size) {
        if (!_buf.empty()) {
            _buf.back().len = 1024;

            will_write.front().len = 1024;
        }

        size_t need = size - remain_bytes;
        while (need > 0) {
            _buf.emplace_back(new char[1024]);

            int writed = need > 1024 ? 1024 : need;
            _buf.back().len += writed;
            need -= writed;
            will_write.emplace_back(_buf.back());

            _buf.back().done = will_write.back().p.get_future();
        }
    } else if (!_buf.empty()) {
        _buf.back().len += size;
        will_write.front().len += size;
    }
    int blocks = _buf.size();
    lock.unlock();

    int writed = 0;
    int index = 0;
    while (writed < size) {
        FileWritePcb& pcb = will_write[index++];

        ::memcpy(pcb.block + pcb.origin_len, 
            ctx + writed, pcb.len - pcb.origin_len);
        writed += pcb.len - pcb.origin_len;

        pcb.p.set_value(true);  /** 告知消费者已完成 */
    }

    if (blocks > 2) {
        _cond.notify_all();
    }
}

void FileWriter::append(const std::string& str) {
    append(str.c_str(), str.size());
}

void FileWriter::append(char* cstr) {
    append(cstr, ::strlen(cstr));
}

void FileWriter::append(int i) {
    std::string istr = util::itoa(i);
    append(istr);
}

void FileWriter::append(double dou) {
    std::string dstr = util::dtoa(dou);
    append(dstr);
}

void FileWriter::append(uint32_t ui32) {
    std::string ui32_str = util::ui32toa(ui32);
    append(ui32_str);
}

void FileWriter::append(uint64_t ui64) {
    std::string ui64_str = util::ui64toa(ui64);
    append(ui64_str);
}

}   // fg