#pragma once

#include "rcs/net/message.hpp"
#include "rcs/net/rate_limiter.hpp"

#include <array>
#include <boost/asio.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace rcs::net {

using ConnectionId = std::uint64_t;

struct ConnectionOptions {
    // 单帧 payload 的硬上限，避免异常请求导致大内存分配。
    std::size_t max_payload_size{FrameCodec::default_max_payload_size};

    // 单连接消息频率限制。
    std::size_t max_messages_per_window{120};
    std::chrono::milliseconds rate_limit_window{1000};
};

// TCP 连接通过回调上报生命周期和已解码消息。
// 网络网关持有这些回调，并负责把事件桥接到上层模块。
struct ConnectionCallbacks {
    std::function<void(ConnectionId)> on_open;
    std::function<void(ConnectionId, const Message&)> on_message;
    std::function<void(ConnectionId, const std::string&)> on_error;
    std::function<void(ConnectionId)> on_close;
};

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    using tcp = boost::asio::ip::tcp;

    // 套接字已经由网络网关接收完成。每个连接独占自己的套接字，
    // 并通过执行序列串行化该连接的异步处理逻辑。
    TcpConnection(boost::asio::io_context& io_context,
                  tcp::socket socket,
                  ConnectionId id,
                  ConnectionOptions options,
                  ConnectionCallbacks callbacks);

    ConnectionId id() const noexcept;
    std::string remote_address() const;
    std::chrono::steady_clock::time_point last_seen_at() const noexcept;

    // 对象进入 shared_ptr 管理之后，启动读循环。
    void start();

    // 将消息加入异步写队列；多次发送会被串行化。
    void send(const Message& message);

    // 在连接 strand 上异步关闭。
    void close();

private:
    // 先读帧头，再根据 payload 大小分配并读取 body。
    void read_header();
    void read_body(FrameHeader header);

    // 每次写出 outbound_queue_ 中的一帧。
    void write_next();

    void close_now();
    void fail(const std::string& reason);

    tcp::socket socket_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    ConnectionId id_{0};
    ConnectionOptions options_;
    ConnectionCallbacks callbacks_;
    RateLimiter limiter_;
    std::array<std::uint8_t, FrameCodec::header_size> inbound_header_{};
    ByteBuffer inbound_body_;
    std::deque<ByteBuffer> outbound_queue_;
    bool closed_{false};
    std::chrono::steady_clock::time_point last_seen_at_;
};

} // namespace rcs::net
