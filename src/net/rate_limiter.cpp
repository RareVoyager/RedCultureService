#include "rcs/net/rate_limiter.hpp"

namespace rcs::net {

RateLimiter::RateLimiter(std::size_t max_events, std::chrono::milliseconds window)
    : max_events_(max_events),
      window_(window),
      window_started_at_(std::chrono::steady_clock::now())
{
}

bool RateLimiter::allow()
{
    if (max_events_ == 0) {
        // 0 表示不限流，方便本地调试或可信链路使用。
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - window_started_at_ >= window_) {
        // 超过配置窗口后开启新的计数窗口。
        window_started_at_ = now;
        used_events_ = 0;
    }

    if (used_events_ >= max_events_) {
        return false;
    }

    ++used_events_;
    return true;
}

void RateLimiter::reset()
{
    used_events_ = 0;
    window_started_at_ = std::chrono::steady_clock::now();
}

} // namespace rcs::net
