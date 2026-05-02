#pragma once

#include "rcs/auth/session_auth_service.hpp"

#include <cstdint>
#include <string>

namespace rcs::server::dto {

struct LoginRequest {
    std::string player_id;
    std::string account;
    std::string token;
    std::uint64_t connection_id{0};
};

struct LoginResponse {
    std::string token;
    auth::Session session;
};

} // namespace rcs::server::dto
