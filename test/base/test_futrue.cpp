#include <future>
#include <vector>

class A {
public: 
    std::promise<bool> p;
};

void test() {
    std::vector<std::future<bool>> fu;
    {
        std::vector<A> list;
        for (int i = 0; i < 10; i++) {
            list.emplace_back();
        }

        std::vector<std::future<bool>> futures(10);
        for (int i = 0; i < 10; ++ i) {
            futures[i] = list[i].p.get_future();
        }

        fu.swap(futures);

        for (int i = 0; i < 10; ++ i) {
            list[i].p.set_value(true);
        }
    }

    for (int i = 0; i < 10; ++ i) {
        fu[i].get();
    }
}

int main(int argc, const char** argv) {
    test();
    return 0;
}