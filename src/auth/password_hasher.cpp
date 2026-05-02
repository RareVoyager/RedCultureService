#include "rcs/auth/password_hasher.hpp"

#if defined(__linux__)
#include <crypt.h>
#endif

#include <array>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <string>

namespace rcs::auth {
namespace {

constexpr char BcryptBase64Alphabet[] =
    "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

std::string bcrypt_base64_encode(const unsigned char* data, std::size_t size)
{
    std::string out;
    out.reserve(22);

    std::size_t i = 0;
    while (i < size) {
        unsigned int c1 = data[i++];
        out.push_back(BcryptBase64Alphabet[(c1 >> 2) & 0x3f]);
        c1 = (c1 & 0x03) << 4;

        if (i >= size) {
            out.push_back(BcryptBase64Alphabet[c1 & 0x3f]);
            break;
        }

        unsigned int c2 = data[i++];
        c1 |= (c2 >> 4) & 0x0f;
        out.push_back(BcryptBase64Alphabet[c1 & 0x3f]);
        c1 = (c2 & 0x0f) << 2;

        if (i >= size) {
            out.push_back(BcryptBase64Alphabet[c1 & 0x3f]);
            break;
        }

        c2 = data[i++];
        c1 |= (c2 >> 6) & 0x03;
        out.push_back(BcryptBase64Alphabet[c1 & 0x3f]);
        out.push_back(BcryptBase64Alphabet[c2 & 0x3f]);
    }

    // bcrypt salt 固定 16 字节输入、22 字符输出。
    if (out.size() > 22) {
        out.resize(22);
    }
    return out;
}

std::string make_bcrypt_salt(int cost)
{
    if (cost < 4 || cost > 31) {
        throw std::invalid_argument("bcrypt cost must be in range [4, 31]");
    }

    std::array<unsigned char, 16> raw_salt{};
    std::random_device random;
    for (auto& byte : raw_salt) {
        byte = static_cast<unsigned char>(random() & 0xffU);
    }

    const auto encoded_salt = bcrypt_base64_encode(raw_salt.data(), raw_salt.size());
    const auto cost_text = cost < 10 ? "0" + std::to_string(cost) : std::to_string(cost);
    return "$2b$" + cost_text + "$" + encoded_salt;
}

bool constant_time_equals(const std::string& lhs, const std::string& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    unsigned char diff = 0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        diff |= static_cast<unsigned char>(lhs[i] ^ rhs[i]);
    }
    return diff == 0;
}

std::string crypt_bcrypt(const std::string& password, const std::string& salt)
{
#if defined(__linux__)
    struct crypt_data data {};
    data.initialized = 0;

    const char* hashed = crypt_r(password.c_str(), salt.c_str(), &data);
    if (hashed == nullptr || std::string(hashed).empty()) {
        throw std::runtime_error("bcrypt crypt_r failed");
    }
    return hashed;
#else
    (void)password;
    (void)salt;
    throw std::runtime_error("bcrypt password hashing requires libxcrypt/crypt on Linux");
#endif
}

} // namespace

std::string hash_password_bcrypt(const std::string& password, BcryptConfig config)
{
    if (password.empty()) {
        throw std::invalid_argument("password is empty");
    }

    return crypt_bcrypt(password, make_bcrypt_salt(config.cost));
}

bool verify_password_bcrypt(const std::string& password, const std::string& password_hash)
{
    if (password.empty() || password_hash.empty()) {
        return false;
    }
    if (password_hash.rfind("$2", 0) != 0) {
        return false;
    }

    try {
        return constant_time_equals(crypt_bcrypt(password, password_hash), password_hash);
    } catch (...) {
        return false;
    }
}

} // namespace rcs::auth
