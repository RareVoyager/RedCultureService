#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace rcs::common {

inline constexpr int RSuccessCode = 200;
inline constexpr int RErrorCode = 500;
inline constexpr const char* RSuccessMsg = "success";
inline constexpr const char* RErrorMsg = "error";

// 通用返回类型：包含 string code,string msg,T data
template <typename T>
class Result {
    static_assert(!std::is_void_v<T>, "Use Result<void> for void results");

public:
    using ValueType = T;

    static Result success()
    {
        Result result;
        result.code_ = RSuccessCode;
        result.msg_ = RSuccessMsg;
        return result;
    }

    static Result success(T data)
    {
        Result result = success();
        result.data_ = std::move(data);
        return result;
    }

    template <typename U = T,
              typename = std::enable_if_t<!std::is_same_v<std::decay_t<U>, std::string>>>
    static Result success(std::string msg)
    {
        Result result = success();
        result.msg_ = std::move(msg);
        return result;
    }

    static Result success(std::string msg, T data)
    {
        Result result = success(std::move(data));
        result.msg_ = std::move(msg);
        return result;
    }

    // 当 T 是 std::string 时，success("xxx") 表示 data 会更符合 C++ 重载规则；
    // 如果只想改成功提示信息，请使用这个显式函数。
    static Result success_msg(std::string msg)
    {
        Result result = success();
        result.msg_ = std::move(msg);
        return result;
    }

    static Result error()
    {
        Result result;
        result.code_ = RErrorCode;
        result.msg_ = RErrorMsg;
        return result;
    }

    static Result error(std::string msg)
    {
        Result result = error();
        result.msg_ = std::move(msg);
        return result;
    }

    static Result error(int code, std::string msg)
    {
        Result result;
        result.code_ = code;
        result.msg_ = std::move(msg);
        return result;
    }

    static Result failure(std::string msg)
    {
        return error(std::move(msg));
    }

    static Result failure(int code, std::string msg)
    {
        return error(code, std::move(msg));
    }

    bool ok() const noexcept
    {
        return code_ >= 200 && code_ < 300;
    }

    explicit operator bool() const noexcept
    {
        return ok();
    }

    int code() const noexcept
    {
        return code_;
    }

    const std::string& msg() const noexcept
    {
        return msg_;
    }

    bool hasData() const noexcept
    {
        return data_.has_value();
    }

    const std::optional<T>& optionalData() const noexcept
    {
        return data_;
    }

    const T& data() const&
    {
        ensureData();
        return *data_;
    }

    T& data() &
    {
        ensureData();
        return *data_;
    }

    T&& data() &&
    {
        ensureData();
        return std::move(*data_);
    }

private:
    Result() = default;

    void ensureData() const
    {
        if (!data_) {
            throw std::logic_error("Result does not contain data: " + std::to_string(code_) + " " + msg_);
        }
    }

    int code_{RErrorCode};
    std::string msg_;
    std::optional<T> data_;
};

// void �ػ�������ֻ���� code/msg������Ҫ data �ĳ�����
template <>
class Result<void> {
public:
    static Result success()
    {
        Result result;
        result.code_ = RSuccessCode;
        result.msg_ = RSuccessMsg;
        return result;
    }

    static Result success(std::string msg)
    {
        Result result = success();
        result.msg_ = std::move(msg);
        return result;
    }

    static Result error()
    {
        Result result;
        result.code_ = RErrorCode;
        result.msg_ = RErrorMsg;
        return result;
    }

    static Result error(std::string msg)
    {
        Result result = error();
        result.msg_ = std::move(msg);
        return result;
    }

    static Result error(int code, std::string msg)
    {
        Result result;
        result.code_ = code;
        result.msg_ = std::move(msg);
        return result;
    }

    static Result failure(std::string msg)
    {
        return error(std::move(msg));
    }

    static Result failure(int code, std::string msg)
    {
        return error(code, std::move(msg));
    }

    bool ok() const noexcept
    {
        return code_ >= 200 && code_ < 300;
    }

    explicit operator bool() const noexcept
    {
        return ok();
    }

    int code() const noexcept
    {
        return code_;
    }

    const std::string& msg() const noexcept
    {
        return msg_;
    }

private:
    Result() = default;

    int code_{RErrorCode};
    std::string msg_;
};

} // namespace rcs::common