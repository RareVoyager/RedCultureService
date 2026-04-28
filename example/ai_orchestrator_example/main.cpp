#include "rcs/ai_orchestrator/ai_orchestrator_service.hpp"

#include <iostream>
#include <memory>

namespace {

class MockAiClient final : public rcs::ai_orchestrator::IAiClient {
public:
    rcs::ai_orchestrator::AiResponse complete(const rcs::ai_orchestrator::AiRequest& request) override {
        if (request.kind == rcs::ai_orchestrator::AiTaskKind::generate_question) {
            return {true,
                    "红色文化互动题：请说出一次重要会议的历史意义。",
                    {},
                    {{"mock", "true"}}};
        }

        if (request.kind == rcs::ai_orchestrator::AiTaskKind::generate_explanation) {
            return {true,
                    "讲解：这个答案体现了玩家对历史事件背景和影响的理解，可以继续引导其补充时间、地点和人物。",
                    {},
                    {{"mock", "true"}}};
        }

        return {true, "提示：请结合场景线索作答。", {}, {{"mock", "true"}}};
    }
};

void print_flow(const rcs::ai_orchestrator::AiInteractionFlow& flow) {
    std::cout << "flow id: " << flow.id
              << ", stage: " << rcs::ai_orchestrator::to_string(flow.stage)
              << '\n';
    if (!flow.generated_question.empty()) {
        std::cout << "question: " << flow.generated_question << '\n';
    }
    if (!flow.generated_explanation.empty()) {
        std::cout << "explanation: " << flow.generated_explanation << '\n';
    }
}

} // namespace

int main() {
    auto client = std::make_shared<MockAiClient>();
    rcs::ai_orchestrator::AiOrchestratorService service(client);

    rcs::ai_orchestrator::AiContext context;
    context.room_id = 1001;
    context.player_id = "player-1";
    context.scene_id = "museum-hall";
    context.topic = "遵义会议";

    auto flow_result = service.start_question_flow(context);
    if (!flow_result.ok || !flow_result.flow) {
        std::cout << "start flow failed: " << flow_result.error << '\n';
        return 1;
    }

    service.tick();
    auto flow = service.find_flow(flow_result.flow->id);
    if (!flow) {
        std::cout << "flow not found\n";
        return 1;
    }
    print_flow(*flow);

    auto answer_result = service.submit_answer(flow->id, "它是一次具有转折意义的重要会议。");
    if (!answer_result.ok) {
        std::cout << "submit answer failed: " << answer_result.error << '\n';
        return 1;
    }

    service.tick();
    flow = service.find_flow(flow->id);
    if (flow) {
        print_flow(*flow);
    }

    rcs::ai_orchestrator::TriggerRule rule;
    rule.id = "enter-museum-hall";
    rule.type = rcs::ai_orchestrator::TriggerType::enter_area;
    rule.scene_id = "museum-hall";
    rule.topic = "红色文化展厅";
    rule.prompt_template = "玩家 {player_id} 进入 {scene_id}，请生成一个关于 {topic} 的引导问题。";
    service.register_trigger_rule(rule);

    const auto triggered_tasks = service.handle_trigger_event({rcs::ai_orchestrator::TriggerType::enter_area, context});
    std::cout << "triggered tasks: " << triggered_tasks.size() << '\n';
    service.tick(triggered_tasks.size());

    return 0;
}
