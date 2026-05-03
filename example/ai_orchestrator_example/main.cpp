#include "rcs/ai_orchestrator/ai_orchestrator_service.hpp"

#include <iostream>
#include <memory>

namespace {

class MockAiClient final : public rcs::ai_orchestrator::IAiClient {
public:
    rcs::ai_orchestrator::AiResponse complete(const rcs::ai_orchestrator::AiRequest& request) override
    {
        if (request.kind == rcs::ai_orchestrator::AiTaskKind::generate_question) {
            return {true,
                    "请结合场景中的红色文化元素，提出一个适合玩家回答的问题。",
                    {},
                    {{"mock", "true"}}};
        }

        if (request.kind == rcs::ai_orchestrator::AiTaskKind::generate_explanation) {
            return {true,
                    "讲解：这个回答体现了玩家对历史事件背景和影响的理解，可以继续引导其补充时间、地点和人物。",
                    {},
                    {{"mock", "true"}}};
        }

        return {true, "提示：请先观察场景，再组织答案。", {}, {{"mock", "true"}}};
    }
};

void printFlow(const rcs::ai_orchestrator::AiInteractionFlow& flow)
{
    std::cout << "flow id: " << flow.id
              << ", stage: " << rcs::ai_orchestrator::toString(flow.stage)
              << '\n';
    if (!flow.generated_question.empty()) {
        std::cout << "question: " << flow.generated_question << '\n';
    }
    if (!flow.generated_explanation.empty()) {
        std::cout << "explanation: " << flow.generated_explanation << '\n';
    }
}

} // namespace

int main()
{
    auto client = std::make_shared<MockAiClient>();
    rcs::ai_orchestrator::AiOrchestratorService service(client);

    rcs::ai_orchestrator::AiContext context;
    context.room_id = 1001;
    context.player_id = "player-1";
    context.scene_id = "museum-hall";
    context.topic = "遵义会议";

    auto flowResult = service.startQuestionFlow(context);
    if (!flowResult.ok || !flowResult.flow) {
        std::cout << "start flow failed: " << flowResult.error << '\n';
        return 1;
    }

    service.tick();
    auto flow = service.findFlow(flowResult.flow->id);
    if (!flow) {
        std::cout << "flow not found\n";
        return 1;
    }
    printFlow(*flow);

    auto answerResult = service.submitAnswer(flow->id, "它是一次具有转折意义的重要会议。");
    if (!answerResult.ok) {
        std::cout << "submit answer failed: " << answerResult.error << '\n';
        return 1;
    }

    service.tick();
    flow = service.findFlow(flow->id);
    if (flow) {
        printFlow(*flow);
    }

    rcs::ai_orchestrator::TriggerRule rule;
    rule.id = "enter-museum-hall";
    rule.type = rcs::ai_orchestrator::TriggerType::enter_area;
    rule.scene_id = "museum-hall";
    rule.topic = "遵义会议精神";
    rule.prompt_template = "玩家 {player_id} 进入 {scene_id}，请生成一个关于 {topic} 的引导问题。";
    service.registerTriggerRule(rule);

    const auto triggeredTasks = service.handleTriggerEvent({rcs::ai_orchestrator::TriggerType::enter_area, context});
    std::cout << "triggered tasks: " << triggeredTasks.size() << '\n';
    service.tick(triggeredTasks.size());

    return 0;
}
