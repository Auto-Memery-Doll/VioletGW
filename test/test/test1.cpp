
#include <functional>
#include <iostream>
#include <ostream>
#include <thread>
class MyClass {
public:
    int x;
};

void dosome(MyClass obj) {
    obj.x = 100;
}

int main(int argc, const char** argv) {
 
    MyClass obj;
    obj.x = 2;

    auto i = std::ref(obj);
    auto j = i.get();
    std::thread t(&dosome, std::ref(obj));
    t.join();

    std::cout << obj.x << std::endl;
    return 0;
}