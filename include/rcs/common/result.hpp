#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace rcs::common {

inline constexpr const char* kOkCode = "OK";
inline constexpr const char* kOkMsg = "success";
inline constexpr const char* kErrorCode = "ERROR";

// 通用返回类型：成功时包含 data，失败时包含 code/msg。
template <typename T>
class Result {
    static_assert(!std::is_void_v<T>, "Use Result<void> for void results");

public:
    using ValueType = T;

    static Result success(T data, std::string code = kOkCode, std::string msg = kOkMsg)
    {
        return Result(true, std::move(code), std::move(msg), std::move(data));
    }

    static Result failure(std::string code, std::string msg)
    {
        return Result(false, std::move(code), std::move(msg), std::nullopt);
    }

    static Result failure(std::string msg)
    {
        return failure(kErrorCode, std::move(msg));
    }

    bool ok() const noexcept
    {
        return ok_;
    }

    explicit operator bool() const noexcept
    {
        return ok();
    }

    const std::string& code() const noexcept
    {
        return code_;
    }

    const std::string& msg() const noexcept
    {
        return msg_;
    }

    bool has_data() const noexcept
    {
        return data_.has_value();
    }

    const std::optional<T>& optional_data() const noexcept
    {
        return data_;
    }

    const T& data() const&
    {
        ensure_data();
        return *data_;
    }

    T& data() &
    {
        ensure_data();
        return *data_;
    }

    T&& data() &&
    {
        ensure_data();
        return std::move(*data_);
    }

private:
    Result(bool ok, std::string code, std::string msg, std::optional<T> data)
        : ok_(ok),
          code_(std::move(code)),
          msg_(std::move(msg)),
          data_(std::move(data))
    {
    }

    void ensure_data() const
    {
        if (!data_) {
            throw std::logic_error("Result does not contain data: " + code_ + " " + msg_);
        }
    }

    bool ok_{false};
    std::string code_;
    std::string msg_;
    std::optional<T> data_;
};

// void 特化：用于只关心成功/失败，不需要 data 的场景。
template <>
class Result<void> {
public:
    static Result success(std::string code = kOkCode, std::string msg = kOkMsg)
    {
        return Result(true, std::move(code), std::move(msg));
    }

    static Result failure(std::string code, std::string msg)
    {
        return Result(false, std::move(code), std::move(msg));
    }

    static Result failure(std::string msg)
    {
        return failure(kErrorCode, std::move(msg));
    }

    bool ok() const noexcept
    {
        return ok_;
    }

    explicit operator bool() const noexcept
    {
        return ok();
    }

    const std::string& code() const noexcept
    {
        return code_;
    }

    const std::string& msg() const noexcept
    {
        return msg_;
    }

private:
    Result(bool ok, std::string code, std::string msg)
        : ok_(ok),
          code_(std::move(code)),
          msg_(std::move(msg))
    {
    }

    bool ok_{false};
    std::string code_;
    std::string msg_;
};

} // namespace rcs::common
