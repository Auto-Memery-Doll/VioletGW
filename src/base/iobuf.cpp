#include "iobuf.hpp"
#include "base/type.hpp"
#include "lwip/arch.h"
#include "lwip/pbuf.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string.h>
#include <string>
#include "lwipopts.h"

namespace fg {
// 
// 从dpdk到lwip的pbuf的配置
const pbuf_type DPDK_TO_LWIP_TYPE = PBUF_RAM;
const pbuf_layer DPDK_TO_LWIP_LAYER = PBUF_RAW;
const u16_t DPDK_TO_LWIP_LEN = 0;

// 
// 应用层传入lwip的配置
const pbuf_type USER_TP_LWIP_TYPE = PBUF_POOL;
const pbuf_layer USER_TO_LWIP_LAYER = PBUF_TRANSPORT;

//
// 增加pbuf的引用计数
static  void PbufRef(pbuf *p) {
    pbuf_ref(p);
}

//
// 当dpdk需要将mbuf转换成pbuf的时候使用这个接口
 pbuf* GetEmptyPbuf() {
    return pbuf_alloc(DPDK_TO_LWIP_LAYER, DPDK_TO_LWIP_LEN, DPDK_TO_LWIP_TYPE);
}

//
// 设置pbuf的相关字段
//      payload     pbuf的有效负载
//      tot_len     pbuf的总长度
//      len         当前pbuf的长度
//      done        回调函数闭包，负责释放对应的pbuf和mbuf
 void SetEmptyPbuf(pbuf *pbuf_, void *payload, u16_t tot_len, u16_t len, void *done) {
    pbuf_->done = done;
    pbuf_->payload = payload;
    pbuf_->tot_len = tot_len;
    pbuf_->len = len;
}

//
// 当应用层需要将数据写入到协议栈中
 pbuf* GetPbufWithPayload(u16_t length) {
    return pbuf_alloc(USER_TO_LWIP_LAYER, length, USER_TP_LWIP_TYPE);
}

//
// 将pbuf的引用计数减一，如果引用计数位0，将pbuf释放
 void PbufFree(pbuf *p) {
    pbuf_free(p);
}

//
// 获取pbuf的总长度
 u16_t PbufLen(pbuf *q) {
    return pbuf_clen(q);
}

// 
// 应用层pbuf的实际长度，除去pbuf的传输层协议的头部长度
 size_t UserPbufLen(pbuf *p) {
    return pbuf_clen(p) - PBUF_TRANSPORT;
}

//
// 在应用层调用，将对应的数据拷贝到pbuf中
 pbuf* PbufClone(pbuf *p) {
    pbuf *q = nullptr;
    q = pbuf_alloc(USER_TO_LWIP_LAYER, p->tot_len, USER_TP_LWIP_TYPE);
    return q;
}

//
// 将pbuf链表的前面的一些数据移除
 size_t PbufPopFront(pbuf *p, size_t size) {
    if (0 != pbuf_remove_header(p, size)) {
        return -1;
    }
    return size;
}

//
// 将pbuf链表尾部的数据移除
 size_t PbufPopBack(pbuf *p, size_t size) {
    if (size <= 0) {
        return 0;
    }
    // 将整个pbuf chain释放
    // 减去头部
    if (size >= UserPbufLen(p)) {
        pbuf_free(p);
        return size;
    }

    pbuf_iter iter = p;

    while (iter && iter->next) {
        u16_t prefix = iter->tot_len;
        u16_t suffix = iter->next->tot_len;
        if (prefix >= size && suffix <= size) {
            // 当前pbuf需要释放的pbuf
            size_t remain_size = size - iter->next->tot_len;
            // 将后面的pbuf释放，对于当前的pbuf逻辑释放
            pbuf_free(iter->next);
            iter->next = NULL;    
            iter->len -= remain_size;
            // 修改整个pbuf的字段
            iter = p;
            while (iter) {
                iter->tot_len -= size;
            }
            // 返回移除的字节的数量
            return size;
        }
        iter = iter->next;        
    }

    return 0;
}

//
// 将|payload|指向的数据添加到pbuf的前端
 pbuf* PbufPushFront(pbuf *p, void *paylaod, size_t size, pbuf_type type) {

    int ret = pbuf_add_header_force(p, size);
    if (ret != 1) { // 延长pbuf的前端成功
        ::memcpy(p->payload, paylaod, size);
        return p;
    }

    // 申请一个pbuf
    pbuf *head_buf = GetPbufWithPayload(size, head_buf);
    ::memcpy(head_buf->payload, paylaod, size);
    head_buf->next = p;

    return head_buf;
}

//
// 将|payload|指向的数据添加到pbuf的尾部
size_t PbufPushBack(pbuf *p, void *payload, size_t size) {
    if (p == nullptr || payload == nullptr) {
        return -1;
    }

    pbuf_iter iter = p;
    while (iter->next) {
        iter->tot_len += size;
        iter = iter->next;
    }

    iter->tot_len += size;
    size_t copy_size = PBUF_POOL_BUFSIZE - iter->len;
    if (copy_size > size) {
        ::memcpy((char*)p->payload + iter->len, payload, size);
        return size;
    }

    ::memcpy((char*)p->payload + iter->len, payload, copy_size);
    pbuf *new_buf = GetPbufWithPayload(size - copy_size, false);    // is not head buf
    iter->next = new_buf;
    new_buf->tot_len = size - copy_size;
    iter->len = size - copy_size;
    ::memcpy((char*)new_buf->payload, (char*)payload + copy_size, new_buf->len);
    return size;
}

//
// 将两个pbuf连接起来，并增加from的引用计数
 void PbufAppend(pbuf *to, pbuf *from) {
    pbuf_chain(to, from);
    pbuf_free(from);
}

pbuf* PbufInsertString(pbuf *p, const std::string& str, int32_t offset) {
    if (offset == 0) {
        // 在前端插入
        return PbufInsertCstr(p, str.c_str(), str.size());
    }
    return PbufInsertCstr(p, str.c_str(), str.size());
}

pbuf* PbufInsertCstr(pbuf *p, const char* cstr, size_t len, int32_t offset) {
    if (offset > p->tot_len) {
        return nullptr;
    }

    pbuf_iter iter = p;
    u16_t tot_len = p->tot_len;

    int32_t prefix, suffix;
    while (iter->next) {
        prefix = tot_len - iter->tot_len;
        suffix = tot_len - iter->next->tot_len;

        if (prefix <= offset && suffix >= offset) {
            break;
        }
        iter->tot_len += len;
        iter = iter->next;
    }

    // pbuf chain
    pbuf *new_buf = GetPbufWithPayload(suffix - offset+len);
    if (new_buf == NULL) {
        return nullptr;
    }

    // copy
    int32_t cur_buf_bytes = 0, copy_len = 0;
    pbuf_iter new_iter;
    pbuf_iter new_chain_tail;

    // 先将iter指向的pbuf剩下的数据拷贝到新pbuf中
    cur_buf_bytes += suffix - offset;
    ::memcpy(new_buf->payload, (char*)iter->payload + (offset-prefix), cur_buf_bytes);
    do
    {
        copy_len = new_iter->len - cur_buf_bytes % new_iter->len;
        ::memcpy((char*)new_iter->payload + cur_buf_bytes % new_iter->len, 
            cstr + cur_buf_bytes, copy_len); 
        
        cur_buf_bytes += copy_len;

        new_chain_tail = new_iter;
        new_iter = new_iter->next;
    } while(new_iter);
    
    // 更新原来的pbuf chain
    new_iter = new_buf;
    while (new_iter) {
        new_iter->tot_len += iter->next->tot_len;
    }
    
    new_chain_tail->next = iter->next;
    iter->next = new_chain_tail;
    iter->tot_len += len;
    iter->len = offset - prefix;
    
    return p;
}

pbuf* PbufFixChar(pbuf *p, const char c, int32_t offset) {
    if (offset > p->len) {
        return nullptr;
    }

    // find
    pbuf_iter iter = p;
    int32_t prefix = 0, suffix = 0;
    while (iter) {
        suffix += iter->len;
        if (prefix <= offset && suffix >= offset) {
            break;
        }
        prefix += iter->len;
        iter = iter->next;
    } 

    int32_t at = offset - prefix;
    (char*)(iter->payload)[at] = c;
    return p;

}
}   // fg

namespace fg {
namespace base {

IOBuf::IOBuf() {
    _bufs.buf_nums = 0;
    _bufs.head = nullptr;
    _bufs.bytes = 0;
}

IOBuf::IOBuf(pbuf *pbuf_) {
    _bufs.head = pbuf_;
    _bufs.bytes = pbuf_->tot_len;

    pbuf_iter it = _bufs.head;
    while (it)
    {
        ++_bufs.buf_nums;
        it = it->next;   
    }
    PbufRef(_bufs.head);
}

IOBuf::~IOBuf() {
    PbufFree(_bufs.head);
}

IOBuf::IOBuf(const IOBuf& iobuf) {
    _bufs = iobuf._bufs;
    PbufFree(_bufs.head);
}

IOBuf& IOBuf::operator=(const IOBuf &iobuf) {
    _bufs = iobuf._bufs;
    PbufFree(_bufs.head);
}

IOBuf::IOBuf(IOBuf&& iobuf) {
    _bufs = iobuf._bufs;
    ::memset(&iobuf, 0, sizeof(iobuf));
}

IOBuf& IOBuf::operator=(IOBuf&& iobuf) {
    _bufs = iobuf._bufs;
    ::memset(&iobuf, 0, sizeof(iobuf));
}

auto IOBuf::pop_back(size_t size) -> size_t {
    return PbufPopBack(_bufs.head, size);
}

auto IOBuf::pop_front(size_t size) -> size_t {
    return PbufPopFront(_bufs.head, size);
}

auto IOBuf::append(const IOBuf& iobuf) -> size_t {
    PbufAppend(_bufs.head, iobuf._bufs.head);
}

auto IOBuf::append(const std::string& str) -> size_t {
    return append(str.c_str(), str.size());
}

auto IOBuf::append(const char *cstr, int32_t size) -> size_t {
    pbuf * ret = PbufInsertCstr(_bufs.head, cstr, size, _bufs.head->tot_len);
    if (ret == nullptr) {
        return -1;
    }
    _bufs.head = ret;
    return size;
}

auto IOBuf::cut_to(IOBuf *iobuf) -> void {

}

auto IOBuf::copy_to(std::string *str) -> size_t {

}

auto IOBuf::copy_to(char *buf) -> size_t {
    
}

auto IOBuf::output_file(int fd, int64_t size) -> size_t {

}

auto IOBuf::output_file(const FILE& f, int64_t size) -> size_t {

}

auto IOBuf::input_file(int fd, int64_t size, off_t offset) -> size_t {

}

auto IOBuf::input_file(const FILE& f, int64_t size, off_t offset) -> size_t {

}

auto IOBuf::size() -> size_t {
    return _bufs.bytes;
}


}   // base
}   // fg