#include "storage_engine.hpp"
#include "base/iobuf.hpp"
#include "base/util.hpp"
#include "base/log.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

namespace fg {
namespace cache {


bool StorageEngine::create_dir(const std::string& path) {
    bool ret;

    try {
        ret = std::filesystem::create_directories(path);
    } catch (const std::exception& e) {
        LOG(consule, error) << e.what() << "\n";
    }
    return ret;
}

int StorageEngine::create_file(const std::string& path) {

    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    //            000400  | 000200  | 000040  | 000020  | 000004  | 000002 == 00666 
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_APPEND, mode);
    if (fd == -1) {
        LOG(consule, error) << "create file [" << path << "]"
            << "failure.\n"
            << "what(): " << strerror(errno) << "\n";
        return -1;
    }
    return fd;
}

bool StorageEngine::remove_dir(const std::string& path) {
    bool ret;
    try {
       ret = std::filesystem::remove(path);
    } catch (const std::exception& e) {
        LOG(consule, error) << e.what() << "\n";
    }    
    return ret;
}

bool StorageEngine::remove_dir_all(const std::string& path) {
    bool ret;
    try {
        ret = std::filesystem::remove_all(path);
    } catch (const std::exception& e) {
        LOG(consule, error) << e.what() << "\n";
    }
    return ret;
}

bool StorageEngine::remove_file(const std::string& path) {
    bool ret;
    try {
        ret = std::filesystem::remove(path);
    } catch (const std::exception& e) {
        LOG(consule, error) << e.what() << "\n";
    }
    return ret;
}

int StorageEngine::open(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        LOG(consule, error) << "open file[" << path << "] failure\n"
            << "what(): " << strerror(errno) << "\n";
        return -1;
    }
    return fd;
}

int StorageEngine::close(int fd) {
    int ret = ::close(fd);
    if (ret == -1) {
        LOG(consule, error) << "close file - fd(" << fd << ")\n"
            << "what() : " << strerror(errno) << "\n";
        return ret;
    }
    return ret;
}

size_t StorageEngine::read(int fd, off_t offset, base::IOBuf* buf, size_t size) {
    return buf->input_file(fd, size, offset);
}

size_t StorageEngine::read(const std::string& path, off_t offset, base::IOBuf* buf, size_t size) {
    int fd = open(path);
    return buf->input_file(fd, buf->size(), 0);
}

size_t StorageEngine::write(int fd, base::IOBuf* buf) {
    return buf->input_file(fd, buf->size(), 0);
}

size_t StorageEngine::write(const std::string& path, base::IOBuf* buf) {
    int fd = open(path);
    return buf->input_file(fd, buf->size(), 0);
}


}   // cache
}   // fg