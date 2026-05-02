#pragma once

#include "rcs/application/service_application.hpp"
#include "rcs/http/http_router.hpp"

#include <memory>

namespace rcs::api::controllers {

// 红色文化互动业务接口控制器：开始互动、提交答案。
class InteractionController : public std::enable_shared_from_this<InteractionController> {
public:
    explicit InteractionController(std::shared_ptr<application::ServiceContext> context);

    void register_routes(http::HttpRouter& router);

private:
    http::HttpResponse start_interaction(const http::HttpRequest& request);
    http::HttpResponse answer_interaction(const http::HttpRequest& request);

    std::shared_ptr<application::ServiceContext> context_;
};

} // namespace rcs::api::controllers
