#include "rcs/net/message.hpp"
#include "rcs/net/network_gateway.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    rcs::net::NetworkGatewayConfig config;
    config.listen_address = "0.0.0.0";
    // 可通过 argv[1] 指定端口，方便本地同时运行多个网关实例。
    config.port = argc > 1 ? static_cast<std::uint16_t>(std::strtoul(argv[1], nullptr, 10)) : 7000;

    rcs::net::NetworkGateway gateway(config);

    gateway.set_on_connection_open([](rcs::net::ConnectionId id) {
        std::cout << "connection open: " << id << '\n';
    });

    gateway.set_on_message([&gateway](rcs::net::ConnectionId id, const rcs::net::Message& message) {
        // 心跳属于协议层消息，收到后回复一个空心跳包。
        if (message.type == rcs::net::MessageType::heartbeat) {
            gateway.send(id, rcs::net::Message{rcs::net::MessageType::heartbeat});
            return;
        }

        // 示例程序对 text 消息做 echo，便于在鉴权/房间模块完成前
        // 使用 nc/python 直接验证帧编解码。
        if (message.type == rcs::net::MessageType::text) {
            const auto text = rcs::net::payload_as_string(message);
            std::cout << "message from " << id << ": " << text << '\n';
            gateway.send(id, rcs::net::make_text_message("echo: " + text));
        }
    });

    gateway.set_on_connection_error([](rcs::net::ConnectionId id, const std::string& reason) {
        std::cout << "connection error: " << id << " reason=" << reason << '\n';
    });

    gateway.set_on_connection_close([](rcs::net::ConnectionId id) {
        std::cout << "connection close: " << id << '\n';
    });

    gateway.start();
    std::cout << "network gateway listening on " << config.listen_address << ':' << config.port << '\n';
    gateway.run();
    return 0;
}
