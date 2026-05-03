#include "redculture_server/application/service_application.hpp"
#include "rcs/http/http_types.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

namespace {

rcs::http::HttpResponse postJson(rcs::application::ServiceApplication& app,
                                  const std::string& path,
                                  const std::string& body,
                                  const std::string& token = {})
{
    rcs::http::HttpRequest request;
    request.method = "POST";
    request.target = path;
    request.path = path;
    request.body = body;
    request.headers["Content-Type"] = "application/json";

    if (!token.empty()) {
        request.headers["Authorization"] = "Bearer " + token;
    }

    return app.router()->route(request);
}

rcs::http::HttpResponse get(rcs::application::ServiceApplication& app, const std::string& path)
{
    rcs::http::HttpRequest request;
    request.method = "GET";
    request.target = path;
    request.path = path;
    return app.router()->route(request);
}

void printResponse(const std::string& title, const rcs::http::HttpResponse& response)
{
    std::cout << "\n[" << title << "] status=" << response.status_code << '\n';
    std::cout << response.body << '\n';
}

} // namespace

int main()
{
    // 示例程序不打开真实端口，而是直接调用 HTTP Router，便于快速验证后端接口和业务模块是否串通�?    rcs::application::ApplicationConfig config;
    config.auth.jwt_secret = "example-secret";
    config.allow_dev_auth = true;

    rcs::application::ServiceApplication app(config);

    const auto health = get(app, "/api/v1/ops/health");
    printResponse("health", health);

    const auto login = postJson(app,
                                 "/api/v1/auth/login",
                                 R"({"player_id":"player_001","account":"unity_editor"})");
    printResponse("login", login);

    const auto login_json = nlohmann::json::parse(login.body);
    const auto token = login_json.value("token", "");

    const auto createRoom = postJson(app,
                                       "/api/v1/rooms/create",
                                       R"({"mode":"story","max_players":4,"auto_start_when_full":true})",
                                       token);
    printResponse("create room", createRoom);

    const auto room_json = nlohmann::json::parse(createRoom.body);
    const auto room_id = room_json["room"].value("room_id", 0);

    const auto interaction_start = postJson(app,
                                             "/api/v1/interactions/start",
                                             std::string(R"({"room_id":)") + std::to_string(room_id) +
                                                 R"(,"scene_id":"museum_hall","trigger_id":"trigger_long_march","topic":"长征精神"})",
                                             token);
    printResponse("interaction start", interaction_start);

    const auto interaction_json = nlohmann::json::parse(interaction_start.body);
    const auto interaction_id = interaction_json.value("interaction_id", 0);

    const auto interaction_answer = postJson(app,
                                              "/api/v1/interactions/answer",
                                              std::string(R"({"interaction_id":)") + std::to_string(interaction_id) +
                                                  R"(,"answer":"我看到了坚定理想信念和团结奋斗�?})",
                                              token);
    printResponse("interaction answer", interaction_answer);

    const auto rooms = get(app, "/api/v1/rooms");
    printResponse("rooms", rooms);

    return 0;
}
