#include "rcs/application/api_routes.hpp"

#include "rcs/api/controllers/auth_controller.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace rcs::application {
namespace {

using json = nlohmann::json;

http::HttpResponse json_response(int status_code, const json& body)
{
    return http::HttpResponse::json(status_code, body.dump());
}

http::HttpResponse error_response(int status_code, std::string code, std::string message)
{
    json body;
    body["ok"] = false;
    body["error"] = std::move(code);
    body["message"] = std::move(message);
    return json_response(status_code, body);
}

std::string lower_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::optional<std::string> find_header(const http::HttpRequest& request, const std::string& name)
{
    const auto expected = lower_copy(name);
    for (const auto& [header_name, header_value] : request.headers) {
        if (lower_copy(header_name) == expected) {
            return header_value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> bearer_token(const http::HttpRequest& request)
{
    const auto header = find_header(request, "Authorization");
    if (!header) {
        return std::nullopt;
    }

    const std::string prefix = "bearer ";
    const auto lowered = lower_copy(*header);
    if (lowered.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    return header->substr(prefix.size());
}

std::optional<std::string> query_value(const http::HttpRequest& request, const std::string& key)
{
    std::size_t begin = 0;
    while (begin <= request.query.size()) {
        const auto end = request.query.find('&', begin);
        const auto item = request.query.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        const auto eq = item.find('=');
        if (eq != std::string::npos && item.substr(0, eq) == key) {
            return item.substr(eq + 1);
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return std::nullopt;
}

bool parse_json_body(const http::HttpRequest& request, json& output, http::HttpResponse& error)
{
    if (request.body.empty()) {
        error = error_response(400, "empty_body", "request body must be a JSON object");
        return false;
    }

    output = json::parse(request.body, nullptr, false);
    if (output.is_discarded() || !output.is_object()) {
        error = error_response(400, "invalid_json", "request body must be a valid JSON object");
        return false;
    }

    return true;
}

std::string read_string_or(const json& body, const char* key, std::string fallback = {})
{
    const auto it = body.find(key);
    if (it == body.end() || !it->is_string()) {
        return fallback;
    }
    return it->get<std::string>();
}

bool read_bool_or(const json& body, const char* key, bool fallback)
{
    const auto it = body.find(key);
    if (it == body.end() || !it->is_boolean()) {
        return fallback;
    }
    return it->get<bool>();
}

std::uint64_t read_uint64_or(const json& body, const char* key, std::uint64_t fallback)
{
    const auto it = body.find(key);
    if (it == body.end()) {
        return fallback;
    }

    if (it->is_number_unsigned()) {
        return it->get<std::uint64_t>();
    }

    if (it->is_number_integer()) {
        const auto value = it->get<std::int64_t>();
        return value < 0 ? fallback : static_cast<std::uint64_t>(value);
    }

    if (it->is_string()) {
        try {
            return static_cast<std::uint64_t>(std::stoull(it->get<std::string>()));
        } catch (...) {
            return fallback;
        }
    }

    return fallback;
}

std::size_t read_size_or(const json& body, const char* key, std::size_t fallback)
{
    return static_cast<std::size_t>(read_uint64_or(body, key, fallback));
}

json session_to_json(const auth::Session& session)
{
    return json{
        {"session_id", session.id},
        {"player_id", session.player_id},
        {"account", session.account},
        {"connection_id", session.connection_id},
    };
}

json room_member_to_json(const room::RoomMember& member)
{
    return json{
        {"player_id", member.player.player_id},
        {"connection_id", member.player.connection_id},
        {"ready", member.ready},
    };
}

json room_to_json(const room::RoomInfo& room_info)
{
    json members = json::array();
    for (const auto& member : room_info.members) {
        members.push_back(room_member_to_json(member));
    }

    return json{
        {"room_id", room_info.id},
        {"mode", room_info.mode},
        {"state", room::to_string(room_info.state)},
        {"max_players", room_info.max_players},
        {"auto_start_when_full", room_info.auto_start_when_full},
        {"members", std::move(members)},
    };
}

json ai_task_to_json(const ai_orchestrator::AiTask& task)
{
    return json{
        {"task_id", task.id},
        {"flow_id", task.flow_id ? json(*task.flow_id) : json(nullptr)},
        {"kind", ai_orchestrator::to_string(task.kind)},
        {"status", ai_orchestrator::to_string(task.status)},
        {"attempts", task.attempts},
        {"prompt", task.rendered_prompt},
        {"text", task.response ? task.response->text : ""},
        {"error", task.last_error},
    };
}

json ai_flow_to_json(const ai_orchestrator::AiInteractionFlow& flow)
{
    return json{
        {"flow_id", flow.id},
        {"stage", ai_orchestrator::to_string(flow.stage)},
        {"room_id", flow.context.room_id},
        {"player_id", flow.context.player_id},
        {"scene_id", flow.context.scene_id},
        {"topic", flow.context.topic},
        {"question_task_id", flow.question_task_id},
        {"explanation_task_id", flow.explanation_task_id},
        {"generated_question", flow.generated_question},
        {"submitted_answer", flow.submitted_answer},
        {"generated_explanation", flow.generated_explanation},
        {"error", flow.error},
    };
}

json interaction_start_to_json(const gameplay::StartInteractionResult& result)
{
    return json{
        {"ok", true},
        {"interaction_id", result.interaction_id},
        {"flow_id", result.flow_id},
        {"question_task_id", result.question_task_id},
        {"player_id", result.player_id},
        {"room_id", result.room_id},
        {"scene_id", result.scene_id},
        {"trigger_id", result.trigger_id},
        {"topic", result.topic},
        {"question", result.question},
        {"storage_saved", result.storage_saved},
    };
}

json interaction_answer_to_json(const gameplay::SubmitAnswerResult& result)
{
    return json{
        {"ok", true},
        {"interaction_id", result.interaction_id},
        {"flow_id", result.flow_id},
        {"explanation_task_id", result.explanation_task_id},
        {"tts_task_id", result.tts_task_id},
        {"player_id", result.player_id},
        {"scene_id", result.scene_id},
        {"trigger_id", result.trigger_id},
        {"topic", result.topic},
        {"question", result.question},
        {"answer", result.answer},
        {"explanation", result.explanation},
        {"audio_id", result.audio_id},
        {"audio_mime_type", result.audio_mime_type},
        {"audio_url", result.audio_id.empty() ? "" : "/api/v1/tts/audio?audio_id=" + result.audio_id},
        {"tts_cache_hit", result.tts_cache_hit},
        {"storage_saved", result.storage_saved},
    };
}

struct ResolvedPlayer {
    bool ok{false};
    std::string player_id;
    http::HttpResponse error;
};

ResolvedPlayer resolve_player(const http::HttpRequest& request,
                              const json& body,
                              const std::shared_ptr<ServiceContext>& context)
{
    auto token = bearer_token(request);
    const auto body_token = read_string_or(body, "token");
    if (!body_token.empty()) {
        token = body_token;
    }

    if (token && !token->empty()) {
        const auto result = context->auth_service->validate_token(*token);
        if (!result.ok || !result.claims) {
            return {false, {}, error_response(401, "invalid_token", result.error.empty() ? "token is invalid" : result.error)};
        }
        return {true, result.claims->player_id, {}};
    }

    const auto player_id = read_string_or(body, "player_id");
    if (context->config.allow_dev_auth && !player_id.empty()) {
        return {true, player_id, {}};
    }

    return {false, {}, error_response(401, "unauthorized", "missing bearer token or player_id in local dev mode")};
}

http::HttpResponse handle_login(const http::HttpRequest& request, const std::shared_ptr<ServiceContext>& context)
{
    json body;
    http::HttpResponse parse_error;
    if (!parse_json_body(request, body, parse_error)) {
        return parse_error;
    }

    auto token = read_string_or(body, "token");
    if (token.empty()) {
        if (!context->config.allow_dev_auth) {
            return error_response(401, "token_required", "production mode requires a verified token");
        }

        const auto player_id = read_string_or(body, "player_id");
        if (player_id.empty()) {
            return error_response(400, "missing_player_id", "player_id is required in local dev login");
        }

        const auto account = read_string_or(body, "account", player_id);
        token = context->auth_service->issue_token(player_id, account);
    }

    auto login = context->auth_service->login_with_token(token, read_uint64_or(body, "connection_id", 0));
    if (!login.ok || !login.session) {
        return error_response(401, "login_failed", login.error.empty() ? "login failed" : login.error);
    }

    json response;
    response["ok"] = true;
    response["token"] = token;
    response["session"] = session_to_json(*login.session);
    return json_response(200, response);
}

http::HttpResponse handle_create_room(const http::HttpRequest& request, const std::shared_ptr<ServiceContext>& context)
{
    json body;
    http::HttpResponse parse_error;
    if (!parse_json_body(request, body, parse_error)) {
        return parse_error;
    }

    auto player = resolve_player(request, body, context);
    if (!player.ok) {
        return player.error;
    }

    room::RoomOptions options;
    options.mode = read_string_or(body, "mode", "default");
    options.max_players = read_size_or(body, "max_players", 4);
    options.auto_start_when_full = read_bool_or(body, "auto_start_when_full", true);

    const room::PlayerRef host{
        player.player_id,
        read_uint64_or(body, "connection_id", 0),
    };

    auto result = context->room_service->create_room(host, options);
    if (!result.ok || !result.room) {
        return error_response(400, "create_room_failed", result.error.empty() ? "create room failed" : result.error);
    }

    return json_response(200, json{{"ok", true}, {"room", room_to_json(*result.room)}});
}

http::HttpResponse handle_join_room(const http::HttpRequest& request, const std::shared_ptr<ServiceContext>& context)
{
    json body;
    http::HttpResponse parse_error;
    if (!parse_json_body(request, body, parse_error)) {
        return parse_error;
    }

    auto player = resolve_player(request, body, context);
    if (!player.ok) {
        return player.error;
    }

    const auto room_id = read_uint64_or(body, "room_id", 0);
    if (room_id == 0) {
        return error_response(400, "missing_room_id", "room_id is required");
    }

    const room::PlayerRef member{
        player.player_id,
        read_uint64_or(body, "connection_id", 0),
    };

    auto result = context->room_service->join_room(room_id, member);
    if (!result.ok || !result.room) {
        return error_response(400, "join_room_failed", result.error.empty() ? "join room failed" : result.error);
    }

    return json_response(200, json{{"ok", true}, {"room", room_to_json(*result.room)}});
}

http::HttpResponse handle_list_rooms(const std::shared_ptr<ServiceContext>& context)
{
    json rooms = json::array();
    for (const auto& room_info : context->room_service->list_rooms(false)) {
        rooms.push_back(room_to_json(room_info));
    }

    return json_response(200, json{{"ok", true}, {"rooms", std::move(rooms)}});
}

http::HttpResponse handle_ai_trigger(const http::HttpRequest& request, const std::shared_ptr<ServiceContext>& context)
{
    json body;
    http::HttpResponse parse_error;
    if (!parse_json_body(request, body, parse_error)) {
        return parse_error;
    }

    auto player = resolve_player(request, body, context);
    if (!player.ok) {
        return player.error;
    }

    ai_orchestrator::AiContext ai_context;
    ai_context.room_id = read_uint64_or(body, "room_id", 0);
    ai_context.player_id = player.player_id;
    ai_context.scene_id = read_string_or(body, "scene_id", "default_scene");
    ai_context.topic = read_string_or(body, "topic", "红色文化");
    ai_context.user_input = read_string_or(body, "user_input");

    const auto question_prompt = read_string_or(body, "question_prompt");
    const auto explanation_prompt = read_string_or(body, "explanation_prompt");

    auto flow_result = context->ai_service->start_question_flow(std::move(ai_context), question_prompt, explanation_prompt);
    if (!flow_result.ok || !flow_result.flow) {
        return error_response(400, "ai_flow_failed", flow_result.error.empty() ? "start ai flow failed" : flow_result.error);
    }

    // 先同步推进一次 mock AI，方便 Unity 端第一次联调就能拿到题目文本。
    const auto tick = context->ai_service->tick(4);
    auto latest_flow = context->ai_service->find_flow(flow_result.flow->id).value_or(*flow_result.flow);

    json succeeded = json::array();
    for (const auto& task : tick.succeeded_tasks) {
        succeeded.push_back(ai_task_to_json(task));
    }

    json failed = json::array();
    for (const auto& task : tick.failed_tasks) {
        failed.push_back(ai_task_to_json(task));
    }

    return json_response(200, json{
        {"ok", true},
        {"flow", ai_flow_to_json(latest_flow)},
        {"succeeded_tasks", std::move(succeeded)},
        {"failed_tasks", std::move(failed)},
    });
}

http::HttpResponse handle_ai_answer(const http::HttpRequest& request, const std::shared_ptr<ServiceContext>& context)
{
    json body;
    http::HttpResponse parse_error;
    if (!parse_json_body(request, body, parse_error)) {
        return parse_error;
    }

    auto player = resolve_player(request, body, context);
    if (!player.ok) {
        return player.error;
    }

    const auto flow_id = read_uint64_or(body, "flow_id", 0);
    if (flow_id == 0) {
        return error_response(400, "missing_flow_id", "flow_id is required");
    }

    const auto existing_flow = context->ai_service->find_flow(flow_id);
    if (!existing_flow) {
        return error_response(404, "flow_not_found", "ai flow was not found");
    }
    if (existing_flow->context.player_id != player.player_id) {
        return error_response(403, "flow_forbidden", "player does not own this ai flow");
    }

    const auto answer = read_string_or(body, "answer");
    if (answer.empty()) {
        return error_response(400, "missing_answer", "answer is required");
    }

    auto flow_result = context->ai_service->submit_answer(flow_id, answer);
    if (!flow_result.ok || !flow_result.flow) {
        return error_response(400, "submit_answer_failed", flow_result.error.empty() ? "submit answer failed" : flow_result.error);
    }

    // 提交答案后同步推进一次，让 mock AI 立即生成讲解，方便 Unity 端看到完整闭环。
    const auto tick = context->ai_service->tick(4);
    auto latest_flow = context->ai_service->find_flow(flow_id).value_or(*flow_result.flow);

    json succeeded = json::array();
    for (const auto& task : tick.succeeded_tasks) {
        succeeded.push_back(ai_task_to_json(task));
    }

    json failed = json::array();
    for (const auto& task : tick.failed_tasks) {
        failed.push_back(ai_task_to_json(task));
    }

    return json_response(200, json{
        {"ok", true},
        {"flow", ai_flow_to_json(latest_flow)},
        {"succeeded_tasks", std::move(succeeded)},
        {"failed_tasks", std::move(failed)},
    });
}

http::HttpResponse handle_interaction_start(const http::HttpRequest& request,
                                            const std::shared_ptr<ServiceContext>& context)
{
    json body;
    http::HttpResponse parse_error;
    if (!parse_json_body(request, body, parse_error)) {
        return parse_error;
    }

    auto player = resolve_player(request, body, context);
    if (!player.ok) {
        return player.error;
    }

    gameplay::StartInteractionRequest interaction;
    interaction.player_id = player.player_id;
    interaction.room_id = read_uint64_or(body, "room_id", 0);
    interaction.scene_id = read_string_or(body, "scene_id");
    interaction.trigger_id = read_string_or(body, "trigger_id");
    interaction.topic = read_string_or(body, "topic", "红色文化");
    interaction.question_prompt_template = read_string_or(body, "question_prompt");

    if (body.contains("metadata") && body["metadata"].is_object()) {
        for (const auto& [key, value] : body["metadata"].items()) {
            if (value.is_string()) {
                interaction.metadata[key] = value.get<std::string>();
            }
        }
    }

    auto result = context->gameplay_service->start_interaction(interaction);
    if (!result.ok) {
        return error_response(400, "interaction_start_failed", result.error);
    }

    return json_response(200, interaction_start_to_json(result));
}

http::HttpResponse handle_interaction_answer(const http::HttpRequest& request,
                                             const std::shared_ptr<ServiceContext>& context)
{
    json body;
    http::HttpResponse parse_error;
    if (!parse_json_body(request, body, parse_error)) {
        return parse_error;
    }

    auto player = resolve_player(request, body, context);
    if (!player.ok) {
        return player.error;
    }

    gameplay::SubmitAnswerRequest answer;
    answer.player_id = player.player_id;
    answer.interaction_id = read_uint64_or(body, "interaction_id", 0);
    answer.flow_id = read_uint64_or(body, "flow_id", 0);
    answer.answer = read_string_or(body, "answer");

    auto result = context->gameplay_service->submit_answer(answer);
    if (!result.ok) {
        return error_response(400, "interaction_answer_failed", result.error);
    }

    return json_response(200, interaction_answer_to_json(result));
}

http::HttpResponse handle_tts_audio(const http::HttpRequest& request,
                                    const std::shared_ptr<ServiceContext>& context)
{
    auto audio_id = query_value(request, "audio_id");

    if (!audio_id || audio_id->empty()) {
        json body;
        http::HttpResponse parse_error;
        if (!request.body.empty() && parse_json_body(request, body, parse_error)) {
            audio_id = read_string_or(body, "audio_id");
        }
    }

    if (!audio_id || audio_id->empty()) {
        return error_response(400, "missing_audio_id", "audio_id is required");
    }

    const auto audio = context->tts_service->find_audio(*audio_id);
    if (!audio) {
        return error_response(404, "audio_not_found", "audio resource was not found or expired");
    }

    std::string bytes(audio->bytes.begin(), audio->bytes.end());
    auto response = http::HttpResponse::text(200, std::move(bytes), audio->mime_type);
    response.headers["Cache-Control"] = "private, max-age=1800";
    return response;
}

http::HttpResponse ops_response_to_http(ops::AdminResponse response)
{
    http::HttpResponse converted;
    converted.status_code = response.status_code;
    converted.content_type = std::move(response.content_type);
    converted.body = std::move(response.body);
    converted.headers = std::move(response.headers);
    return converted;
}

http::HttpResponse handle_ops(const http::HttpRequest& request,
                              const std::shared_ptr<ServiceContext>& context,
                              std::string ops_path)
{
    ops::AdminRequest admin_request;
    admin_request.method = request.method;
    admin_request.path = std::move(ops_path);
    admin_request.body = request.body;
    admin_request.headers = request.headers;
    return ops_response_to_http(context->ops_service->handle_request(admin_request));
}

} // namespace

void register_api_routes(http::HttpRouter& router, std::shared_ptr<ServiceContext> context)
{
    auto auth_controller = std::make_shared<rcs::api::controllers::AuthController>(context);

    router.get("/", [context](const http::HttpRequest&) {
        return json_response(200, json{
            {"ok", true},
            {"service", context->ops_service->config().service_name},
            {"message", "RedCultureService HTTP API is running"},
            {"endpoints", {
                "POST /api/v1/auth/login",
                "POST /api/v1/auth/register",
                "POST /api/v1/rooms/create",
                "POST /api/v1/rooms/join",
                "GET /api/v1/rooms",
                "POST /api/v1/ai/trigger",
                "POST /api/v1/ai/answer",
                "POST /api/v1/interactions/start",
                "POST /api/v1/interactions/answer",
                "GET /api/v1/tts/audio?audio_id=<id>",
                "GET /api/v1/ops/health",
                "GET /api/v1/ops/ready",
                "GET /api/v1/ops/version",
                "GET /api/v1/ops/metrics",
            }},
        });
    });

    router.post("/api/v1/auth/login", [context](const http::HttpRequest& request) {
        return handle_login(request, context);
    });

    auth_controller->register_routes(router);

    router.post("/api/v1/rooms/create", [context](const http::HttpRequest& request) {
        return handle_create_room(request, context);
    });

    router.post("/api/v1/rooms/join", [context](const http::HttpRequest& request) {
        return handle_join_room(request, context);
    });

    router.get("/api/v1/rooms", [context](const http::HttpRequest&) {
        return handle_list_rooms(context);
    });

    router.post("/api/v1/ai/trigger", [context](const http::HttpRequest& request) {
        return handle_ai_trigger(request, context);
    });

    router.post("/api/v1/ai/answer", [context](const http::HttpRequest& request) {
        return handle_ai_answer(request, context);
    });

    router.post("/api/v1/interactions/start", [context](const http::HttpRequest& request) {
        return handle_interaction_start(request, context);
    });

    router.post("/api/v1/interactions/answer", [context](const http::HttpRequest& request) {
        return handle_interaction_answer(request, context);
    });

    router.get("/api/v1/tts/audio", [context](const http::HttpRequest& request) {
        return handle_tts_audio(request, context);
    });

    router.post("/api/v1/tts/audio", [context](const http::HttpRequest& request) {
        return handle_tts_audio(request, context);
    });

    router.get("/api/v1/ops/health", [context](const http::HttpRequest& request) {
        return handle_ops(request, context, "/health");
    });

    router.get("/api/v1/ops/ready", [context](const http::HttpRequest& request) {
        return handle_ops(request, context, "/ready");
    });

    router.get("/api/v1/ops/version", [context](const http::HttpRequest& request) {
        return handle_ops(request, context, "/version");
    });

    router.get("/api/v1/ops/metrics", [context](const http::HttpRequest& request) {
        return handle_ops(request, context, "/metrics");
    });

    router.post("/api/v1/ops/shutdown", [context](const http::HttpRequest& request) {
        return handle_ops(request, context, "/shutdown");
    });
}

} // namespace rcs::application
