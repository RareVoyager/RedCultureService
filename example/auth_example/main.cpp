#include "rcs/auth/session_auth_service.hpp"

#include <iostream>

int main() {
    rcs::auth::AuthConfig config;
    config.jwt_secret = "local-dev-secret";

    rcs::auth::SessionAuthService auth(config);

    // 模拟账号密码已由业务层校验通过，这里只负责签发身份令牌�?    const auto token = auth.issueToken("player-10001", "demo_account");
    std::cout << "token: " << token << '\n';

    const auto auth_result = auth.validateToken(token);
    if (!auth_result.ok) {
        std::cout << "validate failed: " << auth_result.error << '\n';
        return 1;
    }

    const auto login_result = auth.loginWithToken(token, 42);
    if (!login_result.ok || !login_result.session) {
        std::cout << "login failed: " << login_result.error << '\n';
        return 1;
    }

    const auto session = *login_result.session;
    std::cout << "session id: " << session.id << '\n';
    std::cout << "player id: " << session.player_id << '\n';
    std::cout << "account: " << session.account << '\n';
    std::cout << "connection id: " << session.connection_id << '\n';
    std::cout << "session count: " << auth.sessionCount() << '\n';

    auth.touchSession(session.id);
    auth.closeSession(session.id);
    std::cout << "session count after close: " << auth.sessionCount() << '\n';

    return 0;
}
