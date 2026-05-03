#include <iostream>

#include "rcs/common/service.hpp"

int main() {
    std::cout << "Hello from " << rcs::common::serviceName() << '\n';
    return 0;
}
