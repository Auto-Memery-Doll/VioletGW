#ifndef FLOW_GATEWAY_LOG_HPP
#define FLOW_GATEWAY_LOG_HPP

#include "base/file_writer.hpp"
#include "base/util.hpp"
#include "base/singleton.hpp"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
namespace fg {

/** 将日志的内容输出到终端中 */
#define FG_consule_LOG_TEMPLATE(level)  \
    template<typename Param>    \
    level& operator<<(Param&& p) {   \
        std::cout << p;    \
        return *this;   \
    }    

#define FG_consule_LOG_USER(level)  

/** 类定义 */
FG_LOG_CLASS(consule)
    FG_LOG_FACTORY(consule, info) {
        _info << "[info]\n";
        return _info;
    }

    FG_LOG_FACTORY(consule, debug) {
        _debug << "[debug]";
        FG_LOG_FORMAT(debug);
        return _debug;
    }

    FG_LOG_FACTORY(consule, warnning) {
        _warnning << "[warnning]";
        FG_LOG_FORMAT(warnning);
        return _warnning;
    }

    FG_LOG_FACTORY(consule, error) {
        _error << "[error]";
        FG_LOG_FORMAT(error);
        return _error;
    }

FG_LOG_CLASS_END(consule, "consule_logger")

/** 将日志的内容输出到文件中 */
/** 输出的方式 */
#define FG_file_LOG_TEMPLATE(level)    \
    template<typename Param>    \
    level& operator<<(Param&& p) {   \
        _writer->append(std::forward<Param>(p));\
        return *this;   \
    }

/** 日志类定制化内容 */
#define FG_file_LOG_USER(level) \
private:    \
    FileWriter *_writer;\
/** 构造函数 */ \
public: \
    level() = default;  \
    void init(FileWriter *writer) {\
        _writer = writer;\
    }   


/** 类定义 */
FG_LOG_CLASS(file)
    void init(const std::string& name) {
        _file_writer = new FileWriter(name);
        _file_writer->init();
    }

    FG_LOG_FACTORY(file, info) {
        _info << "[info]\n";
        return _info;
    }

    FG_LOG_FACTORY(file, debug) {
        _debug << "[debug]";
        FG_LOG_FORMAT(debug);
        return _debug;
    }

    FG_LOG_FACTORY(file, warnning) {
        _warnning << "[warnning]";
        FG_LOG_FORMAT(warnning);
        return _warnning;
    }

    FG_LOG_FACTORY(file, error) {
        _error << "[error]";
        FG_LOG_FORMAT(error);
        return _error;
    }

private:
    file_logger()
    :   _file_writer(new FileWriter)
    {
        _info.init(_file_writer);
        _debug.init(_file_writer);
        _warnning.init(_file_writer);
        _error.init(_file_writer);
    }

private:
    FileWriter *_file_writer;

FG_LOG_CLASS_END(file, "file_log")

#define LOG_FILE_INIT(name) \
fg::file_logger::GetInstance()->init(name)

}   // fg

#endif // !FLOW_GATEWAY_LOG_HPP