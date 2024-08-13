#ifndef FLOW_GATEWAY_OBJ_FACTORY_HPP
#define FLOW_GATEWAY_OBJ_FACTORY_HPP

#include "base/singleton.hpp"
#include <map>
#include <memory>
#include <string>
namespace fg {
namespace base {

template<typename T>
class Factory : public Singletion<Factory<T>> {
    friend class Singletion<Factory<T>>;
public:
    using ptr = std::shared_ptr<T>;
    ~Factory() = default;

    /** 注册一个子类 */
    template<typename SubType>
    void reg(const std::string& name, T&& value) {
        _map[name] = value;
    }

    T* get(const std::string& name) {
        return _map[name];
    }

private:
    Factory() = default;

private: 
    std::map<std::string, T> _map;
};

template <typename T>
inline auto factory() -> Factory<T>::ptr {
    return Factory<T>::GetInstance();
} 

}   // base
}   // fg


#endif // !FLOW_GATEWAY_OBJ_FACTORY_HPP