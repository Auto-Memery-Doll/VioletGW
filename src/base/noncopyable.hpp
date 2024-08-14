#ifndef FLOW_GATEWAY_NONCOPYABLE_HPP
#define FLOW_GATEWAY_NONCOPYABLE_HPP

#define DISABLE_MOVE(T)     \   
T(T&&) = delete;            \
T& operator=(T&&) = delete

#define DISABLE_COPY(T)     \
T(const T&) = delete;       \
T& operator=(const T&) = delete

namespace fg {
namespace base {

class noncopyable {
public:
    noncopyable() = default;
    ~noncopyable() = default;
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator=(const noncopyable&) = delete;
};

}   // base
}   // fg
#endif // !FLOW_GATEWAY_NONCOPYABLE_HPP