#pragma once

#include "redculture_server/application/service_application.hpp"
#include "rcs/http/http_router.hpp"

#include <memory>

namespace rcs::api::controllers {

class InteractionController : public std::enable_shared_from_this<InteractionController> {
public:
    explicit InteractionController(std::shared_ptr<application::ServiceContext> context);

    void registerRoutes(http::HttpRouter& router);

private:
    http::HttpResponse startInteraction(const http::HttpRequest& request);
    http::HttpResponse answerInteraction(const http::HttpRequest& request);

    std::shared_ptr<application::ServiceContext> context_;
};

} // namespace rcs::api::controllers