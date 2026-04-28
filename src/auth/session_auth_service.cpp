#include "rcs/auth/session_auth_service.hpp"

#include <jwt-cpp/traits/nlohmann-json/defaults.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace rcs::auth {

namespace {

std::string claim_as_string(const jwt::decoded_jwt<jwt::traits::nlohmann_json>& token,
                            const std::string& name) {
    if (!token.has_payload_claim(name)) {
        return {};
    }
    return token.get_payload_claim(name).as_string();
}

} // namespace

SessionAuthService::SessionAuthService(AuthConfig config)
    : config_(std::move(config)) {}

const AuthConfig& SessionAuthService::config() const noexcept {
    return config_;
}

std::string SessionAuthService::issue_token(const std::string& player_id, const std::string& account) const {
    if (player_id.empty()) {
        throw std::invalid_argument("player_id is empty");
    }

    const auto now = std::chrono::system_clock::now();
    const auto expires_at = now + config_.token_ttl;

    // 当前使用 HS256，部署时需要把密钥放到配置或环境变量中。
    return jwt::create()
        .set_type("JWT")
        .set_issuer(config_.issuer)
        .set_audience(config_.audience)
        .set_subject(player_id)
        .set_issued_at(now)
        .set_expires_at(expires_at)
        .set_payload_claim("account", jwt::claim(account))
        .sign(jwt::algorithm::hs256{config_.jwt_secret});
}

AuthResult SessionAuthService::validate_token(const std::string& token) const {
    if (token.empty()) {
        return AuthResult{false, "token is empty", std::nullopt};
    }

    try {
        const auto decoded = jwt::decode(token);

        // verifier 会校验签名、签发者、接收方和 exp 等标准字段。
        jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{config_.jwt_secret})
            .with_issuer(config_.issuer)
            .with_audience(config_.audience)
            .verify(decoded);

        TokenClaims claims;
        claims.player_id = decoded.get_subject();
        claims.account = claim_as_string(decoded, "account");
        claims.issued_at = decoded.get_issued_at();
        claims.expires_at = decoded.get_expires_at();

        if (claims.player_id.empty()) {
            return AuthResult{false, "token subject is empty", std::nullopt};
        }

        return AuthResult{true, {}, claims};
    } catch (const std::exception& e) {
        return AuthResult{false, e.what(), std::nullopt};
    }
}

LoginResult SessionAuthService::login_with_token(const std::string& token, std::uint64_t connection_id) {
    const auto auth = validate_token(token);
    if (!auth.ok || !auth.claims) {
        return LoginResult{false, auth.error, std::nullopt};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    return LoginResult{true, {}, upsert_session(*auth.claims, connection_id)};
}

std::optional<Session> SessionAuthService::find_session(SessionId session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<Session> SessionAuthService::find_session_by_player(const std::string& player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto id_it = player_sessions_.find(player_id);
    if (id_it == player_sessions_.end()) {
        return std::nullopt;
    }

    const auto session_it = sessions_.find(id_it->second);
    if (session_it == sessions_.end()) {
        return std::nullopt;
    }
    return session_it->second;
}

bool SessionAuthService::touch_session(SessionId session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;
    }

    it->second.last_seen_at = std::chrono::steady_clock::now();
    return true;
}

bool SessionAuthService::close_session(SessionId session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;
    }

    player_sessions_.erase(it->second.player_id);
    sessions_.erase(it);
    return true;
}

void SessionAuthService::sweep_expired_sessions() {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();

    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (is_session_expired(it->second, now)) {
            player_sessions_.erase(it->second.player_id);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

std::size_t SessionAuthService::session_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

bool SessionAuthService::is_session_expired(const Session& session,
                                            std::chrono::steady_clock::time_point now) const {
    const auto idle_expired = now - session.last_seen_at > config_.session_idle_timeout;
    const auto token_expired = std::chrono::system_clock::now() >= session.token_expires_at;
    return idle_expired || token_expired;
}

Session SessionAuthService::upsert_session(const TokenClaims& claims, std::uint64_t connection_id) {
    const auto now = std::chrono::steady_clock::now();
    const auto existing = player_sessions_.find(claims.player_id);

    if (existing != player_sessions_.end()) {
        auto& session = sessions_.at(existing->second);
        session.account = claims.account;
        session.connection_id = connection_id;
        session.last_seen_at = now;
        session.token_expires_at = claims.expires_at;
        return session;
    }

    Session session;
    session.id = next_session_id_++;
    session.player_id = claims.player_id;
    session.account = claims.account;
    session.connection_id = connection_id;
    session.created_at = now;
    session.last_seen_at = now;
    session.token_expires_at = claims.expires_at;

    player_sessions_[session.player_id] = session.id;
    sessions_[session.id] = session;
    return session;
}

} // namespace rcs::auth
