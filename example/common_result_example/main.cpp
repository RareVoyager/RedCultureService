#include "rcs/common/result.hpp"

#include <iostream>
#include <string>

namespace {

rcs::common::Result<int> divide(int lhs, int rhs)
{
    if (rhs == 0) {
        return rcs::common::Result<int>::failure("DIVIDE_BY_ZERO", "rhs must not be zero");
    }

    return rcs::common::Result<int>::success(lhs / rhs);
}

rcs::common::Result<void> validate_player_id(const std::string& player_id)
{
    if (player_id.empty()) {
        return rcs::common::Result<void>::failure("EMPTY_PLAYER_ID", "player_id is required");
    }

    return rcs::common::Result<void>::success();
}

} // namespace

int main()
{
    const auto divided = divide(8, 2);
    if (divided.ok()) {
        std::cout << "divide result: " << divided.data() << '\n';
    }

    const auto failed = divide(8, 0);
    if (!failed) {
        std::cout << "divide failed: " << failed.code() << " " << failed.msg() << '\n';
    }

    const auto validated = validate_player_id("player_001");
    std::cout << "validate player: " << validated.code() << " " << validated.msg() << '\n';

    return 0;
}
