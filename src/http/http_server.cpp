#include "rcs/http/http_server.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rcs::http {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace beast_http = boost::beast::http;
using tcp = asio::ip::tcp;

std::pair<std::string, std::string> splitTarget(const std::string& target)
{
    const auto pos = target.find('?');
    if (pos == std::string::npos) {
        return {target.empty() ? "/" : target, {}};
    }
    return {target.substr(0, pos), target.substr(pos + 1)};
}

std::string endpointToString(const tcp::endpoint& endpoint)
{
    return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
}

void logAccess(const beast_http::request<beast_http::string_body>& request,
                const tcp::endpoint& remote_endpoint,
                int status_code,
                std::chrono::milliseconds elapsed)
{
    const auto method = std::string(request.method_string());
    const auto target = std::string(request.target());
    const auto remote = endpointToString(remote_endpoint);

    if (status_code >= 500) {
        spdlog::error("http_access method={} target={} status={} elapsed_ms={} remote={}",
                      method,
                      target,
                      status_code,
                      elapsed.count(),
                      remote);
        return;
    }

    if (status_code >= 400) {
        spdlog::warn("http_access method={} target={} status={} elapsed_ms={} remote={}",
                     method,
                     target,
                     status_code,
                     elapsed.count(),
                     remote);
        return;
    }

    spdlog::info("http_access method={} target={} status={} elapsed_ms={} remote={}",
                 method,
                 target,
                 status_code,
                 elapsed.count(),
                 remote);
}

HttpRequest toHttpRequest(const beast_http::request<beast_http::string_body>& request,
                            const tcp::endpoint& remote_endpoint)
{
    HttpRequest converted;
    converted.method = std::string(request.method_string());
    converted.target = std::string(request.target());

    auto [path, query] = splitTarget(converted.target);
    converted.path = std::move(path);
    converted.query = std::move(query);
    converted.body = request.body();
    converted.remoteAddress = remote_endpoint.address().to_string();
    converted.remote_port = remote_endpoint.port();

    for (const auto& field : request) {
        converted.headers.emplace(std::string(field.name_string()), std::string(field.value()));
    }

    return converted;
}

beast_http::response<beast_http::string_body> toBeastResponse(const HttpResponse& response,
                                                                unsigned version,
                                                                bool keep_alive,
                                                                bool enable_cors)
{
    beast_http::response<beast_http::string_body> converted{
        static_cast<beast_http::status>(response.status_code),
        version,
    };

    converted.set(beast_http::field::server, "RedCultureService");
    converted.set(beast_http::field::content_type, response.content_type);

    for (const auto& [name, value] : response.headers) {
        converted.set(name, value);
    }

    // Unity WebGL 会经过浏览器 CORS 校验；桌面和移动端 Unity 不依赖它。
    if (enable_cors) {
        converted.set("Access-Control-Allow-Origin", "*");
        converted.set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        converted.set("Access-Control-Allow-Headers", "Authorization, Content-Type");
    }

    converted.body() = response.body;
    converted.keep_alive(keep_alive);
    converted.prepare_payload();
    return converted;
}

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket socket, std::shared_ptr<HttpRouter> router, HttpServerConfig config)
        : socket_(std::move(socket)),
          router_(std::move(router)),
          config_(std::move(config))
    {
    }

    void run()
    {
        readRequest();
    }

private:
    void readRequest()
    {
        parser_ = std::make_unique<beast_http::request_parser<beast_http::string_body>>();
        parser_->body_limit(config_.max_body_bytes);

        auto self = shared_from_this();
        beast_http::async_read(socket_, buffer_, *parser_, [self](beast::error_code error, std::size_t) {
            self->onRead(error);
        });
    }

    void onRead(beast::error_code error)
    {
        if (error == beast_http::error::end_of_stream) {
            closeSocket();
            return;
        }

        if (error) {
            closeSocket();
            return;
        }

        auto request = parser_->release();
        parser_.reset();

        const auto started_at = std::chrono::steady_clock::now();
        beast::error_code endpoint_error;
        auto remote_endpoint = socket_.remote_endpoint(endpoint_error);
        if (endpoint_error) {
            remote_endpoint = tcp::endpoint{};
        }

        HttpResponse response;
        if (request.method() == beast_http::verb::options) {
            response = HttpResponse::noContent();
        } else {
            try {
                response = router_->route(toHttpRequest(request, remote_endpoint));
            } catch (const std::exception& ex) {
                spdlog::error("http_handler_exception location={}:{} function={} msg={}",
                              __FILE__,
                              __LINE__,
                              __func__,
                              ex.what());
                response = HttpResponse::internalError(ex.what());
            } catch (...) {
                spdlog::error("http_handler_exception location={}:{} function={} msg=unknown server error",
                              __FILE__,
                              __LINE__,
                              __func__);
                response = HttpResponse::internalError("unknown server error");
            }
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at);
        logAccess(request, remote_endpoint, response.status_code, elapsed);

        writeResponse(toBeastResponse(response, request.version(), request.keep_alive(), config_.enable_cors));
    }

    void writeResponse(beast_http::response<beast_http::string_body> response)
    {
        auto shared_response = std::make_shared<beast_http::response<beast_http::string_body>>(std::move(response));
        const auto close_after_write = shared_response->need_eof();

        auto self = shared_from_this();
        beast_http::async_write(socket_, *shared_response, [self, shared_response, close_after_write](beast::error_code error,
                                                                                                      std::size_t) {
            if (error || close_after_write) {
                self->closeSocket();
                return;
            }

            self->readRequest();
        });
    }

    void closeSocket()
    {
        beast::error_code ignored;
        socket_.shutdown(tcp::socket::shutdown_send, ignored);
        socket_.close(ignored);
    }

    tcp::socket socket_;
    beast::flat_buffer buffer_;
    std::unique_ptr<beast_http::request_parser<beast_http::string_body>> parser_;
    std::shared_ptr<HttpRouter> router_;
    HttpServerConfig config_;
};

} // namespace

class HttpServer::Impl {
public:
    Impl(HttpServerConfig config, std::shared_ptr<HttpRouter> router)
        : config_(std::move(config)),
          router_(std::move(router)),
          acceptor_(io_context_)
    {
        if (!router_) {
            throw std::invalid_argument("HttpServer requires a router");
        }
    }

    ~Impl()
    {
        stop();
    }

    void start()
    {
        if (running_.exchange(true)) {
            return;
        }

        beast::error_code error;
        const auto address = asio::ip::make_address(config_.listen_address, error);
        if (error) {
            running_ = false;
            throw std::runtime_error("invalid listen address: " + error.message());
        }

        const tcp::endpoint endpoint{address, config_.listen_port};
        acceptor_.open(endpoint.protocol(), error);
        if (error) {
            running_ = false;
            throw std::runtime_error("open acceptor failed: " + error.message());
        }

        acceptor_.set_option(asio::socket_base::reuse_address(true), error);
        if (error) {
            running_ = false;
            throw std::runtime_error("set reuse_address failed: " + error.message());
        }

        acceptor_.bind(endpoint, error);
        if (error) {
            running_ = false;
            throw std::runtime_error("bind failed: " + error.message());
        }

        acceptor_.listen(asio::socket_base::max_listen_connections, error);
        if (error) {
            running_ = false;
            throw std::runtime_error("listen failed: " + error.message());
        }

        acceptNext();

        const auto thread_count = std::max<std::size_t>(1, config_.thread_count);
        threads_.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i) {
            threads_.emplace_back([this] {
                io_context_.run();
            });
        }
    }

    void stop()
    {
        if (!running_.exchange(false)) {
            return;
        }

        beast::error_code ignored;
        acceptor_.close(ignored);
        io_context_.stop();
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        threads_.clear();
    }

    bool isRunning() const
    {
        return running_.load();
    }

private:
    void acceptNext()
    {
        acceptor_.async_accept(asio::make_strand(io_context_), [this](beast::error_code error, tcp::socket socket) {
            if (!running_) {
                return;
            }

            if (!error) {
                std::make_shared<HttpSession>(std::move(socket), router_, config_)->run();
            }

            acceptNext();
        });
    }

    HttpServerConfig config_;
    std::shared_ptr<HttpRouter> router_;
    asio::io_context io_context_;
    tcp::acceptor acceptor_;
    std::atomic_bool running_{false};
    std::vector<std::thread> threads_;
};

HttpServer::HttpServer(HttpServerConfig config, std::shared_ptr<HttpRouter> router)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(router)))
{
}

HttpServer::~HttpServer() = default;

void HttpServer::start()
{
    impl_->start();
}

void HttpServer::stop()
{
    impl_->stop();
}

bool HttpServer::isRunning() const
{
    return impl_->isRunning();
}

} // namespace rcs::http
