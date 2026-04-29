#pragma once

#include "rcs/http/http_router.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace rcs::http {

struct HttpServerConfig {
    // 监听地址。0.0.0.0 方便虚拟机、Unity 编辑器和局域网设备访问。
    std::string listen_address{"0.0.0.0"};
    std::uint16_t listen_port{8080};

    // HTTP 工作线程数量。当前业务 handler 很轻，默认 2 个足够本地联调。
    std::size_t thread_count{2};

    // 单个请求体最大字节数，避免错误客户端发送过大的 JSON。
    std::size_t max_body_bytes{1024 * 1024};

    // Unity WebGL 需要 CORS；桌面/移动端 Unity 不需要，但打开后更利于统一联调。
    bool enable_cors{true};
};

// 基于 Boost.Beast 的轻量 HTTP Server，负责接收请求并交给 HttpRouter。
class HttpServer {
public:
    HttpServer(HttpServerConfig config, std::shared_ptr<HttpRouter> router);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void start();
    void stop();
    bool is_running() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rcs::http
