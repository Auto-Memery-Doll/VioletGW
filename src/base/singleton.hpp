#ifndef FLOW_GATEWAY_SINGLETON_HPP
#define FLOW_GATEWAY_SINGLETON_HPP

#include "base/noncopyable.hpp"
#include <memory>
#include <mutex>

template <typename T>
class Singletion : public noncopyable {
public:
    DISABLE_MOVE(Singletion<T>);

    static inline auto GetInstance() -> std::shared_ptr<T> {
        static std::once_flag flag;
        std::call_once(flag, [](){
            _obj = std::shared_ptr<T>(new T);
        });
        return _obj;
    }

private:
    Singletion() = default;
    ~Singletion() = default;

private:
    static std::shared_ptr<T> _obj;
};


#endif // !FLOW_GATEWAY_SINGLETON_HPP