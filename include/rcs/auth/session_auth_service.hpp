#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace rcs::auth {

using SessionId = std::uint64_t;

struct AuthConfig {
    // JWT 签发者，用于校验 token 是否来自当前服务。
    std::string issuer{"red-culture-service"};

    // JWT 接收方，用于区分不同客户端或服务。
    std::string audience{"unity-client"};

    // HS256 签名密钥。生产环境应从配置中心或环境变量读取。
    std::string jwt_secret{"change-me-in-production"};

    // token 有效期。
    std::chrono::seconds token_ttl{std::chrono::hours(2)};

    // 会话最大空闲时间，超过后会被清理。
    std::chrono::seconds session_idle_timeout{std::chrono::minutes(30)};
};

struct TokenClaims {
    std::string player_id;
    std::string account;
    std::chrono::system_clock::time_point issued_at{};
    std::chrono::system_clock::time_point expires_at{};
};

struct AuthResult {
    bool ok{false};
    std::string error;
    std::optional<TokenClaims> claims;
};

struct Session {
    SessionId id{0};
    std::string player_id;
    std::string account;

    // 网络连接 id 由接入层传入，0 表示暂未绑定连接。
    std::uint64_t connection_id{0};

    std::chrono::steady_clock::time_point created_at{};
    std::chrono::steady_clock::time_point last_seen_at{};
    std::chrono::system_clock::time_point token_expires_at{};
};

struct LoginResult {
    bool ok{false};
    std::string error;
    std::optional<Session> session;
};

class SessionAuthService {
public:
    explicit SessionAuthService(AuthConfig config = {});

    const AuthConfig& config() const noexcept;

    // 签发一个 JWT。当前先接收已验证的玩家身份，账号密码校验后续接入 storage。
    std::string issue_token(const std::string& player_id, const std::string& account) const;

    // 校验 JWT 签名、签发者、接收方和过期时间，并返回解析后的 claims。
    AuthResult validate_token(const std::string& token) const;

    // 使用 token 创建或刷新内存会话。
    LoginResult login_with_token(const std::string& token, std::uint64_t connection_id = 0);

    std::optional<Session> find_session(SessionId session_id) const;
    std::optional<Session> find_session_by_player(const std::string& player_id) const;

    bool touch_session(SessionId session_id);
    bool close_session(SessionId session_id);
    void sweep_expired_sessions();

    std::size_t session_count() const;

private:
    bool is_session_expired(const Session& session, std::chrono::steady_clock::time_point now) const;
    Session upsert_session(const TokenClaims& claims, std::uint64_t connection_id);

    AuthConfig config_;
    SessionId next_session_id_{1};
    std::unordered_map<SessionId, Session> sessions_;
    std::unordered_map<std::string, SessionId> player_sessions_;
    mutable std::mutex mutex_;
};

} // namespace rcs::auth
