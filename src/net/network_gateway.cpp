#include "rcs/net/network_gateway.hpp"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/post.hpp>
#include <utility>

namespace rcs::net {

NetworkGateway::NetworkGateway(NetworkGatewayConfig config)
    : config_(std::move(config)),
      acceptor_(io_context_),
      strand_(io_context_.get_executor()) {}

NetworkGateway::~NetworkGateway() {
    stop();
}

void NetworkGateway::start() {
    if (running_) {
        return;
    }

    // 重启上下文，允许同一个网关实例在停止之后再次启动。
    io_context_.restart();

    const auto address = boost::asio::ip::make_address(config_.listen_address);
    const tcp::endpoint endpoint(address, config_.port);

    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();

    running_ = true;
    accept_next();
}

void NetworkGateway::stop() {
    if (!running_) {
        return;
    }

    // 关闭接收器会取消尚未完成的异步接收。
    running_ = false;
    boost::system::error_code ignored;
    acceptor_.close(ignored);

    for (auto& [_, connection] : connections_) {
        connection->close();
    }
    connections_.clear();
    io_context_.stop();
}

void NetworkGateway::run() {
    io_context_.run();
}

void NetworkGateway::poll() {
    io_context_.poll();
}

bool NetworkGateway::is_running() const noexcept {
    return running_;
}

std::size_t NetworkGateway::connection_count() const {
    return connections_.size();
}

const NetworkGatewayConfig& NetworkGateway::config() const noexcept {
    return config_;
}

boost::asio::io_context& NetworkGateway::io_context() noexcept {
    return io_context_;
}

void NetworkGateway::send(ConnectionId id, const Message& message) {
    // 通过网关执行序列访问连接表，保证读操作安全。
    boost::asio::post(strand_, [this, id, message]() {
        const auto it = connections_.find(id);
        if (it != connections_.end()) {
            it->second->send(message);
        }
    });
}

void NetworkGateway::broadcast(const Message& message) {
    // 广播采用尽力而为策略；已关闭连接会通过自身 close 回调移除。
    boost::asio::post(strand_, [this, message]() {
        for (auto& [_, connection] : connections_) {
            connection->send(message);
        }
    });
}

void NetworkGateway::set_on_connection_open(std::function<void(ConnectionId)> callback) {
    callbacks_.on_open = std::move(callback);
}

void NetworkGateway::set_on_message(std::function<void(ConnectionId, const Message&)> callback) {
    callbacks_.on_message = std::move(callback);
}

void NetworkGateway::set_on_connection_error(std::function<void(ConnectionId, const std::string&)> callback) {
    callbacks_.on_error = std::move(callback);
}

void NetworkGateway::set_on_connection_close(std::function<void(ConnectionId)> callback) {
    callbacks_.on_close = std::move(callback);
}

void NetworkGateway::accept_next() {
    acceptor_.async_accept(boost::asio::bind_executor(
        strand_,
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!running_) {
                return;
            }

            if (!ec) {
                if (connections_.size() >= config_.max_connections) {
                    boost::system::error_code ignored;
                    socket.close(ignored);
                } else {
                    // 在当前进程内分配单调递增的连接 id。
                    const auto id = next_connection_id_++;
                    auto connection_callbacks = callbacks_;

                    // 先从连接表移除，再转发 close 给外部观察者；
                    // 这样外部读取 connection_count() 时已经是最新状态。
                    connection_callbacks.on_close = [this, original = callbacks_.on_close](ConnectionId closed_id) {
                        boost::asio::post(strand_, [this, original, closed_id]() {
                            remove_connection(closed_id);
                            if (original) {
                                original(closed_id);
                            }
                        });
                    };

                    auto connection = std::make_shared<TcpConnection>(
                        io_context_,
                        std::move(socket),
                        id,
                        config_.connection_options,
                        std::move(connection_callbacks));

                    connections_.emplace(id, connection);
                    connection->start();
                }
            }

            accept_next();
        }));
}

void NetworkGateway::remove_connection(ConnectionId id) {
    // 擦除共享指针后，连接对象会在所有未完成处理函数释放
    // 自身共享引用后自然析构。
    connections_.erase(id);
}

} // namespace rcs::net
