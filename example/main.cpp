#include <iostream>

#include "rcs/common/service.hpp"

int main() {
    std::cout << "Hello from " << rcs::common::service_name() << '\n';
    return 0;
}
