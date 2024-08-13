
#include <any>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <utility>

template <typename T>
class TestFactory {

public:
    T* get(const std::string& name) {
        return _factory[name]();
    }

    template<typename Func>
    void reg(const std::string & name, Func func) {
        _factory[name] = func;
    }

private:
    std::map<std::string, std::function<T*(void)>> _factory;
};


class Base {
public:
    virtual void do_something() = 0;
};

class A : public Base {
public:
    void do_something() override {
        std::cout << "this is class A\n";
    }

    static Base* make() {
        return new A();
    }
};

class B : public Base {
public:
    void do_something() override {
        std::cout << "this is class B\n";
    }

    static Base* make() {
        return new B();
    }
};

class C : public Base {
public:
    void do_something() override {
        std::cout << "this is class C\n";
    }

    static Base* make() {
        return new C();
    }
};

void test() {
    TestFactory<Base> factory;
    std::string A_name = "A";
    std::string B_name = "B";
    std::string C_name = "C";
    factory.reg(A_name, A::make);
    factory.reg(B_name, B::make);
    factory.reg(C_name, C::make);

    auto obj_A = factory.get(A_name);
    auto obj_B = factory.get(B_name);
    auto obj_C = factory.get(C_name);

    obj_A->do_something();
    obj_B->do_something();
    obj_C->do_something();
    
    return;
}

int main(int argc, const char** argv) {
 
    test();
    return 0;
}