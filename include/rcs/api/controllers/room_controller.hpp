#pragma once

#include "rcs/application/service_application.hpp"
#include "rcs/http/http_router.hpp"

#include <memory>

namespace rcs::api::controllers {

// 房间相关 HTTP 接口控制器：创建、加入、查询房间。
class RoomController : public std::enable_shared_from_this<RoomController> {
public:
    explicit RoomController(std::shared_ptr<application::ServiceContext> context);

    void register_routes(http::HttpRouter& router);

private:
    http::HttpResponse create_room(const http::HttpRequest& request);
    http::HttpResponse join_room(const http::HttpRequest& request);
    http::HttpResponse list_rooms(const http::HttpRequest& request);

    std::shared_ptr<application::ServiceContext> context_;
};

} // namespace rcs::api::controllers
