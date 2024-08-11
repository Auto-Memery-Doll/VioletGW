#include "base/trie.hpp"
#include <iostream>
#include <string>


void test() {
    fg::base::Trie<std::string> string_tree;


    while (true) {
        std::string command;
        std::string key, value;
        std::cin >> command;
        std::cout << "[info] -> command:" << command << std::endl;
        if (command == "get") {
            std::cin >> key;
            auto ret = string_tree.get(key);
            std::cout << *ret << std::endl;
        } else if (command == "set") {
            std::cin >> key >> value;
            auto ret = string_tree.set(key, &value);
            if (ret) {
                std::cout << "set success" << std::endl;
            } else {
                std::cout << "the key is exist\n";
            }
        }
    }
}


int main(int argc, const char** argv) {

    test();

    return 0;
}