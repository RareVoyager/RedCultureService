#pragma once

namespace rcs::auth {

class SessionAuthService {
public:
    bool validate_token() const;
};

} // namespace rcs::auth
