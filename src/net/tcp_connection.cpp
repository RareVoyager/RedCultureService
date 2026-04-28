#include "rcs/net/tcp_connection.hpp"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <utility>

namespace rcs::net {

TcpConnection::TcpConnection(boost::asio::io_context& io_context,
                             tcp::socket socket,
                             ConnectionId id,
                             ConnectionOptions options,
                             ConnectionCallbacks callbacks)
    : socket_(std::move(socket)),
      strand_(io_context.get_executor()),
      id_(id),
      options_(options),
      callbacks_(std::move(callbacks)),
      limiter_(options_.max_messages_per_window, options_.rate_limit_window),
      last_seen_at_(std::chrono::steady_clock::now()) {}

ConnectionId TcpConnection::id() const noexcept {
    return id_;
}

std::string TcpConnection::remote_address() const {
    boost::system::error_code ignored;
    const auto endpoint = socket_.remote_endpoint(ignored);
    if (ignored) {
        return {};
    }
    return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
}

std::chrono::steady_clock::time_point TcpConnection::last_seen_at() const noexcept {
    return last_seen_at_;
}

void TcpConnection::start() {
    if (callbacks_.on_open) {
        callbacks_.on_open(id_);
    }

    // 读循环按 header -> body -> header 的顺序持续执行，直到连接关闭。
    read_header();
}

void TcpConnection::send(const Message& message) {
    auto self = shared_from_this();
    boost::asio::post(strand_, [self, encoded = FrameCodec::encode(message)]() mutable {
        // 只有队列中的第一帧会启动 async_write；
        // 后续帧会在当前写完成后由 write_next() 继续发送。
        const bool write_in_progress = !self->outbound_queue_.empty();
        self->outbound_queue_.push_back(std::move(encoded));
        if (!write_in_progress) {
            self->write_next();
        }
    });
}

void TcpConnection::close() {
    auto self = shared_from_this();
    boost::asio::post(strand_, [self]() {
        self->close_now();
    });
}

void TcpConnection::read_header() {
    auto self = shared_from_this();
    boost::asio::async_read(socket_,
                            boost::asio::buffer(inbound_header_),
                            boost::asio::bind_executor(strand_,
                                                        [self](boost::system::error_code ec, std::size_t) {
                                                            if (ec) {
                                                                self->close_now();
                                                                return;
                                                            }

                                                            try {
                                                                // 分配 inbound_body_ 之前先校验 payload 大小。
                                                                const auto header = FrameCodec::decode_header(
                                                                    self->inbound_header_.data(),
                                                                    self->options_.max_payload_size);
                                                                self->read_body(header);
                                                            } catch (const std::exception& e) {
                                                                self->fail(e.what());
                                                            }
                                                        }));
}

void TcpConnection::read_body(FrameHeader header) {
    // 消息体大小已经由帧头解析逻辑校验过。
    inbound_body_.assign(header.payload_size, 0);

    auto self = shared_from_this();
    boost::asio::async_read(socket_,
                            boost::asio::buffer(inbound_body_),
                            boost::asio::bind_executor(strand_,
                                                        [self, header](boost::system::error_code ec, std::size_t) {
                                                            if (ec) {
                                                                self->close_now();
                                                                return;
                                                            }

                                                            if (!self->limiter_.allow()) {
                                                                // 提前关闭过于频繁的连接。
                                                                // 更高层的限流策略后续仍可加在 NetworkGateway 之上。
                                                                self->fail("connection rate limit exceeded");
                                                                return;
                                                            }

                                                            self->last_seen_at_ = std::chrono::steady_clock::now();

                                                            Message message;
                                                            message.type = header.type;
                                                            message.flags = header.flags;
                                                            // 将收到的 payload 移入回调消息，避免额外拷贝。
                                                            message.payload = std::move(self->inbound_body_);

                                                            if (self->callbacks_.on_message) {
                                                                self->callbacks_.on_message(self->id_, message);
                                                            }

                                                            self->read_header();
                                                        }));
}

void TcpConnection::write_next() {
    if (outbound_queue_.empty() || closed_) {
        return;
    }

    // 保持队首缓冲区存活，直到异步写完成。
    auto self = shared_from_this();
    boost::asio::async_write(socket_,
                             boost::asio::buffer(outbound_queue_.front()),
                             boost::asio::bind_executor(strand_,
                                                         [self](boost::system::error_code ec, std::size_t) {
                                                             if (ec) {
                                                                 self->close_now();
                                                                 return;
                                                             }

                                                             self->outbound_queue_.pop_front();
                                                             self->write_next();
                                                         }));
}

void TcpConnection::close_now() {
    if (closed_) {
        return;
    }

    // 如果对端已经断开，关闭发送和接收方向可能失败；
    // 这里仍继续 close()，并有意忽略这两个错误。
    closed_ = true;
    boost::system::error_code ignored;
    socket_.shutdown(tcp::socket::shutdown_both, ignored);
    socket_.close(ignored);

    if (callbacks_.on_close) {
        callbacks_.on_close(id_);
    }
}

void TcpConnection::fail(const std::string& reason) {
    if (callbacks_.on_error) {
        callbacks_.on_error(id_, reason);
    }
    close_now();
}

} // namespace rcs::net
