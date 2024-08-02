#ifndef FLOW_GATEWAY_IOBUF_HPP
#define FLOW_GATEWAY_IOBUF_HPP

#include "lwip/pbuf.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <string>

namespace fg {

// 对pbuf的封装
pbuf* GetEmptyPbuf();
void SetEmptyPbuf(pbuf *pbuf_, void *payload, u16_t tot_len, u16_t len, void *done);
pbuf* GetPbufWithPayload(u16_t length, bool is_head);
void PbufFree(pbuf *p);
u16_t PbufLen(pbuf *q);
size_t UserPbufLen(pbuf *p);
pbuf* PbufClone(pbuf *p);
size_t PbufPopFront(pbuf *p, size_t size);
size_t PbufPopBack(pbuf *p, size_t size);
pbuf* PbufPushFront(pbuf *p, void *paylaod, size_t size, pbuf_type type);
size_t PbufPushBack(pbuf *p, void *payload, size_t size);
void PbufAppend(pbuf *to, pbuf *from);
pbuf* PbufInsertString(pbuf *p, const std::string& str, int32_t offset = -1);
pbuf* PbufInsertCstr(pbuf *p, const char* cstr, size_t len, int32_t offset = -1);
pbuf* PbufFixChar(pbuf *p, const char c, int32_t offset = -1);

namespace base {

// 零拷贝缓冲区
// 能够随意拷贝
class IOBuf {
    const static int64_t size_hint = 1024 * 1024;
public:
    IOBuf();
    IOBuf(pbuf *pbuf_);
    ~IOBuf();
    IOBuf(const IOBuf& iobuf);
    IOBuf& operator=(const IOBuf& iobuf);
    IOBuf(IOBuf&& iobuf);
    IOBuf& operator=(IOBuf&& iobuf);

    /* stl api */
    // pop_front只需移动pbuf->fg_iobuf_beign指针即可
    // 避免了移动整块内存
    auto pop_front(size_t size) -> size_t;
    auto pop_back(size_t size) -> size_t;

    /* append */
    // 如果是IOBuf只需增加引用技术即可，无需拷贝
    auto append(const IOBuf& iobuf) -> size_t;
    // 需要拷贝
    auto append(const std::string& str) -> size_t;
    auto append(const char* cstr, int32_t size) -> size_t;

    /* cut */
    auto cut_to(IOBuf *iobuf) -> void;

    /* output */
    auto copy_to(std::string* str) -> size_t;
    auto copy_to(char *buf) -> size_t;

    /* 文件操作 */
    // offset 为io的其实偏移量
    auto output_file(int fd, int64_t size = size_hint) -> size_t;
    auto output_file(const FILE& f, int64_t size = size_hint) -> size_t;
    auto input_file(int fd, int64_t size = size_hint, off_t offset = 0) -> size_t;
    auto input_file(const FILE& f, int64_t size = size_hint, off_t offset = 0) -> size_t;

    /* 有效字节的数量 */
    auto size() -> size_t;

private:
    /* IOBuf对应用层用户开发的，而在dpdk和应用层之间有lwip，所以应用层使用的是lwip的pbuf */
    /* 数据面使用的dpkdk的mbuf */
    struct PbufLink {
        struct pbuf *head;   /* 头尾指针 */
        int32_t buf_nums;  /* iobuf中包含的pbuf的数量 */
        int64_t bytes;     /* iobuf中的有效的字节数 */
    };

    PbufLink _bufs;
};

// for debug
inline std::ostream& operator <<(std::ostream& os, const IOBuf& iobuf) {

    return os;
}
}   // base 
}   // fg
#endif // !FLOW_GATEWAY_IOBUF_HPP