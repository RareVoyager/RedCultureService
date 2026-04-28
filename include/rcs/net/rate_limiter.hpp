#pragma once

#include <chrono>
#include <cstddef>

namespace rcs::net {

// 简单的固定窗口限流器，用于控制单个连接的消息频率。
// 当前先保持轻量；IP 级别或全局限流后续可以在更上层扩展。
class RateLimiter {
public:
    RateLimiter(std::size_t max_events, std::chrono::milliseconds window);

    bool allow();
    void reset();

private:
    std::size_t max_events_{0};
    std::chrono::milliseconds window_{1000};
    std::size_t used_events_{0};
    std::chrono::steady_clock::time_point window_started_at_;
};

} // namespace rcs::net
