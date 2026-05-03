#pragma once

#include "redculture_server/application/service_application.hpp"
#include "rcs/http/http_router.hpp"

#include <memory>

namespace rcs::api::controllers {

class RoomController : public std::enable_shared_from_this<RoomController> {
public:
    explicit RoomController(std::shared_ptr<application::ServiceContext> context);

    void registerRoutes(http::HttpRouter& router);

private:
    http::HttpResponse createRoom(const http::HttpRequest& request);
    http::HttpResponse joinRoom(const http::HttpRequest& request);
    http::HttpResponse listRooms(const http::HttpRequest& request);

    std::shared_ptr<application::ServiceContext> context_;
};

} // namespace rcs::api::controllers