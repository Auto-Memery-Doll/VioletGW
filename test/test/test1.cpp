
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
 
    std::cout << "EIO: " << EIO << std::endl;
    std::cout << "ENODEV: " << ENODEV << std::endl;
    std::cout << "EINVAL: " << EINVAL << std::endl;
    std::cout << "ENOMEM: " << ENOMEM << std::endl; 
    return 0;
}
