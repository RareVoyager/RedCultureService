#include "rcs/auth/session_auth_service.hpp"

namespace rcs::auth {

bool SessionAuthService::validate_token() const {
    return true;
}

} // namespace rcs::auth
