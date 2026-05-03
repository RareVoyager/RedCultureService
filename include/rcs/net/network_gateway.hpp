#pragma once

#include "rcs/net/message.hpp"
#include "rcs/net/tcp_connection.hpp"

#include <boost/asio.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace rcs::net {

struct NetworkGatewayConfig {
    // 传输控制协议监听地址和端口。
    std::string listen_address{"0.0.0.0"};
    std::uint16_t port{7000};

    // 在正式接入准入控制模块前，先提供一个基础连接数保护。
    std::size_t max_connections{4096};
    ConnectionOptions connection_options;
};

// 网络网关是网络接入层的公共入口，负责接收 TCP 客户端并管理活跃连接。
class NetworkGateway {
public:
    explicit NetworkGateway(NetworkGatewayConfig config = {});
    ~NetworkGateway();

    void start();
    void stop();
    void run();
    void poll();

    bool isRunning() const noexcept;
    std::size_t connectionCount() const;
    const NetworkGatewayConfig& config() const noexcept;

    boost::asio::io_context& ioContext() noexcept;

    // 向指定连接发送消息。
    void send(ConnectionId id, const Message& message);

    // 向所有活跃连接广播消息。
    void broadcast(const Message& message);

    void setOnConnectionOpen(std::function<void(ConnectionId)> callback);
    void setOnMessage(std::function<void(ConnectionId, const Message&)> callback);
    void setOnConnectionError(std::function<void(ConnectionId, const std::string&)> callback);
    void setOnConnectionClose(std::function<void(ConnectionId)> callback);

private:
    using tcp = boost::asio::ip::tcp;

    // 持续接收新连接，直到 stop() 关闭 acceptor。
    void acceptNext();
    void removeConnection(ConnectionId id);

    NetworkGatewayConfig config_;
    boost::asio::io_context io_context_;
    tcp::acceptor acceptor_;

    // 所有连接表的读写都通过 strand 串行化。
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    std::unordered_map<ConnectionId, std::shared_ptr<TcpConnection>> connections_;
    ConnectionCallbacks callbacks_;
    ConnectionId next_connection_id_{1};
    bool running_{false};
};

} // namespace rcs::net
