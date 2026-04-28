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

// 网络网关是网络接入层的公共入口。
// 它负责接收 TCP 客户端、管理活跃连接，并向鉴权/会话/房间等上层模块暴露协议层回调。
class NetworkGateway {
public:
    explicit NetworkGateway(NetworkGatewayConfig config = {});
    ~NetworkGateway();

    void start();
    void stop();
    void run();
    void poll();

    bool is_running() const noexcept;
    std::size_t connection_count() const;
    const NetworkGatewayConfig& config() const noexcept;

    boost::asio::io_context& io_context() noexcept;

    // 向指定连接发送消息。
    void send(ConnectionId id, const Message& message);

    // 向所有活跃连接广播消息。
    void broadcast(const Message& message);

    void set_on_connection_open(std::function<void(ConnectionId)> callback);
    void set_on_message(std::function<void(ConnectionId, const Message&)> callback);
    void set_on_connection_error(std::function<void(ConnectionId, const std::string&)> callback);
    void set_on_connection_close(std::function<void(ConnectionId)> callback);

private:
    using tcp = boost::asio::ip::tcp;

    // 持续接收新连接，直到 stop() 关闭 acceptor。
    void accept_next();
    void remove_connection(ConnectionId id);

    NetworkGatewayConfig config_;
    boost::asio::io_context io_context_;
    tcp::acceptor acceptor_;

    // 所有连接表的读写都通过这个 strand 串行化。
    // 后续即使用多个线程运行 io_context，也能保持连接管理逻辑安全。
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    std::unordered_map<ConnectionId, std::shared_ptr<TcpConnection>> connections_;
    ConnectionCallbacks callbacks_;
    ConnectionId next_connection_id_{1};
    bool running_{false};
};

} // namespace rcs::net
