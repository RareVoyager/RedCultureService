#pragma once

#include "rcs/auth/session_auth_service.hpp"

#include <cstdint>
#include <string>

namespace rcs::api::dto {

// 注册请求 DTO：承接 Unity/Apifox 传入的账号资料。
struct RegisterRequest {
    std::string player_id;
    std::string account;
    std::string password;
    std::string display_name;
    std::string avatar_url;
    std::uint64_t connection_id{0};
};

// 登录请求 DTO：支持 token 登录，也支持账号密码登录。
struct LoginRequest {
    std::string player_id;
    std::string account;
    std::string password;
    std::string token;
    std::uint64_t connection_id{0};
};

// 鉴权响应 DTO：返回签发/校验后的 token、服务端会话和基础用户信息。
struct AuthResponse {
    std::string token;
    auth::Session session;
    std::string display_name;
    std::string avatar_url;
};

using LoginResponse = AuthResponse;

} // namespace rcs::api::dto
