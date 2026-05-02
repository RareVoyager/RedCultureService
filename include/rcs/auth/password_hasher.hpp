#pragma once

#include <string>

namespace rcs::auth {

struct BcryptConfig {
    // bcrypt cost 越高越安全但越慢。12 是 Web 服务常用起点。
    int cost{12};
};

// 使用 bcrypt 生成密码哈希，返回标准 crypt 格式，例如：$2b$12$...
std::string hash_password_bcrypt(const std::string& password, BcryptConfig config = {});

// 使用存量 bcrypt hash 校验明文密码。
bool verify_password_bcrypt(const std::string& password, const std::string& password_hash);

} // namespace rcs::auth
