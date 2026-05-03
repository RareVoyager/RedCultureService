#include "redculture_server/application/service_application.hpp"
#include "rcs/gameplay/cultural_interaction_service.hpp"

#include <iostream>
#include <string>

int main()
{
    // 业务示例：不�?HTTP，直接通过 ApplicationContext 调用 Gameplay 服务�?    rcs::application::ApplicationConfig config;
    config.auth.jwt_secret = "gameplay-example-secret";
    config.gameplay.require_room_membership = true;

    rcs::application::ServiceApplication app(config);
    const auto context = app.context();

    const auto token = context->auth_service->issueToken("player_001", "unity_editor");
    const auto login = context->auth_service->loginWithToken(token);
    if (!login.ok) {
        std::cerr << "login failed: " << login.error << '\n';
        return 1;
    }

    auto room = context->room_service->createRoom({"player_001", 0}, {"story", 4, true});
    if (!room.ok || !room.room) {
        std::cerr << "create room failed: " << room.error << '\n';
        return 1;
    }

    rcs::gameplay::StartInteractionRequest start;
    start.player_id = "player_001";
    start.room_id = room.room->id;
    start.scene_id = "museum_hall";
    start.trigger_id = "trigger_long_march";
    start.topic = "长征精神";

    const auto started = context->gameplay_service->startInteraction(start);
    if (!started.ok) {
        std::cerr << "start interaction failed: " << started.error << '\n';
        return 1;
    }

    std::cout << "interaction id: " << started.interaction_id << '\n';
    std::cout << "question: " << started.question << '\n';

    rcs::gameplay::SubmitAnswerRequest answer;
    answer.player_id = "player_001";
    answer.interaction_id = started.interaction_id;
    answer.answer = "我看到了坚定理想信念、团结奋斗和不怕困难的长征精神�?;

    const auto submitted = context->gameplay_service->submitAnswer(answer);
    if (!submitted.ok) {
        std::cerr << "submit answer failed: " << submitted.error << '\n';
        return 1;
    }

    std::cout << "explanation: " << submitted.explanation << '\n';
    std::cout << "audio id: " << submitted.audio_id << '\n';
    std::cout << "audio mime: " << submitted.audio_mime_type << '\n';
    std::cout << "storage saved: " << (submitted.storage_saved ? "true" : "false") << '\n';

    return 0;
}
