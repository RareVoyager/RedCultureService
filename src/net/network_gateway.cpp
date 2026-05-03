#include "rcs/net/network_gateway.hpp"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/post.hpp>

#include <utility>

namespace rcs::net {

NetworkGateway::NetworkGateway(NetworkGatewayConfig config)
    : config_(std::move(config)),
      acceptor_(io_context_),
      strand_(io_context_.get_executor())
{
}

NetworkGateway::~NetworkGateway()
{
    stop();
}

void NetworkGateway::start()
{
    if (running_) {
        return;
    }

    // 重启上下文，允许同一个网关实例在停止后再次启动。
    io_context_.restart();

    const auto address = boost::asio::ip::make_address(config_.listen_address);
    const tcp::endpoint endpoint(address, config_.port);

    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();

    running_ = true;
    acceptNext();
}

void NetworkGateway::stop()
{
    if (!running_) {
        return;
    }

    running_ = false;
    boost::system::error_code ignored;
    acceptor_.close(ignored);

    for (auto& [_, connection] : connections_) {
        connection->close();
    }
    connections_.clear();
    io_context_.stop();
}

void NetworkGateway::run()
{
    io_context_.run();
}

void NetworkGateway::poll()
{
    io_context_.poll();
}

bool NetworkGateway::isRunning() const noexcept
{
    return running_;
}

std::size_t NetworkGateway::connectionCount() const
{
    return connections_.size();
}

const NetworkGatewayConfig& NetworkGateway::config() const noexcept
{
    return config_;
}

boost::asio::io_context& NetworkGateway::ioContext() noexcept
{
    return io_context_;
}

void NetworkGateway::send(ConnectionId id, const Message& message)
{
    // 通过网关 strand 访问连接表，保证跨线程调用时安全。
    boost::asio::post(strand_, [this, id, message]() {
        const auto it = connections_.find(id);
        if (it != connections_.end()) {
            it->second->send(message);
        }
    });
}

void NetworkGateway::broadcast(const Message& message)
{
    // 广播采用尽力而为策略；已关闭连接会通过自身 close 回调移除。
    boost::asio::post(strand_, [this, message]() {
        for (auto& [_, connection] : connections_) {
            connection->send(message);
        }
    });
}

void NetworkGateway::setOnConnectionOpen(std::function<void(ConnectionId)> callback)
{
    callbacks_.on_open = std::move(callback);
}

void NetworkGateway::setOnMessage(std::function<void(ConnectionId, const Message&)> callback)
{
    callbacks_.on_message = std::move(callback);
}

void NetworkGateway::setOnConnectionError(std::function<void(ConnectionId, const std::string&)> callback)
{
    callbacks_.on_error = std::move(callback);
}

void NetworkGateway::setOnConnectionClose(std::function<void(ConnectionId)> callback)
{
    callbacks_.on_close = std::move(callback);
}

void NetworkGateway::acceptNext()
{
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
                    const auto id = next_connection_id_++;
                    auto connectionCallbacks = callbacks_;

                    // 先从连接表移除，再通知外部观察者，避免 connectionCount() 读到旧状态。
                    connectionCallbacks.on_close = [this, original = callbacks_.on_close](ConnectionId closedId) {
                        boost::asio::post(strand_, [this, original, closedId]() {
                            removeConnection(closedId);
                            if (original) {
                                original(closedId);
                            }
                        });
                    };

                    auto connection = std::make_shared<TcpConnection>(
                        io_context_,
                        std::move(socket),
                        id,
                        config_.connection_options,
                        std::move(connectionCallbacks));

                    connections_.emplace(id, connection);
                    connection->start();
                }
            }

            acceptNext();
        }));
}

void NetworkGateway::removeConnection(ConnectionId id)
{
    connections_.erase(id);
}

} // namespace rcs::net
