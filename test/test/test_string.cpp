
#include <iostream>
#include <string>
void test() {
    std::string str1 = "1", str2 = "2";
    bool res = str1 < str2;
    std::cout << res << std::endl;
}

int main(int argc, const char** argv) {
    test();
    return 0;
}