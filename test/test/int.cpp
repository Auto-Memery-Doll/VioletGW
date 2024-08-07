#include <iostream>

enum {
	e = 0x80000000,
};

int main(int argc, char **argv) {
	std::cout << e << std::endl;
}

