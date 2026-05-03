#pragma once

#include "redculture_server/application/service_application.hpp"
#include "rcs/http/http_router.hpp"

#include <memory>

namespace rcs::api::controllers {

class TtsController : public std::enable_shared_from_this<TtsController> {
public:
    explicit TtsController(std::shared_ptr<application::ServiceContext> context);

    void registerRoutes(http::HttpRouter& router);

private:
    http::HttpResponse getAudio(const http::HttpRequest& request);

    std::shared_ptr<application::ServiceContext> context_;
};

} // namespace rcs::api::controllers