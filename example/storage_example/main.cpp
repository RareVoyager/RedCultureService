#include "rcs/storage/storage_service.hpp"

#include <cstdlib>
#include <iostream>

namespace {

std::string readConnectionUri() {
    const char* env = std::getenv("RCS_POSTGRES_URI");
    if (env && *env) {
        return env;
    }

    return "postgresql://postgres:postgres@127.0.0.1:5432/redculture";
}

} // namespace

int main() {
    rcs::storage::StorageConfig config;
    config.connection_uri = readConnectionUri();
    config.auto_migrate = true;

    rcs::storage::StorageService storage(config);

    const auto connected = storage.connect();
    if (!connected.ok) {
        std::cout << "connect failed: " << connected.error << '\n';
        std::cout << "set RCS_POSTGRES_URI to your PostgreSQL connection string\n";
        return 1;
    }

    rcs::storage::UserProfile user;
    user.player_id = "player-10001";
    user.account = "demo_account";
    user.display_name = "Demo Player";
    user.metadata = {{"source", "storage_example"}};

    const auto user_result = storage.upsertUser(user);
    if (!user_result.ok) {
        std::cout << "upsert user failed: " << user_result.error << '\n';
        return 1;
    }

    rcs::storage::AnswerRecord answer;
    answer.player_id = user.player_id;
    answer.question_id = "q-001";
    answer.question = "遵义会议的重要意义是什么？";
    answer.answer = "它在关键时刻挽救了革命?";
    answer.correct = true;
    answer.score = 95.0;
    answer.metadata = {{"scene_id", "museum-hall"}};

    const auto answer_result = storage.appendAnswerRecord(answer);
    if (!answer_result.ok) {
        std::cout << "append answer failed: " << answer_result.error << '\n';
        return 1;
    }

    rcs::storage::ProgressRecord progress;
    progress.player_id = user.player_id;
    progress.scene_id = "museum-hall";
    progress.progress = {{"chapter", 1}, {"completed", true}};
    storage.saveProgress(progress);

    rcs::storage::EventLog event;
    event.level = "info";
    event.category = "storage_example";
    event.message = "storage module example completed";
    event.metadata = {{"player_id", user.player_id}};
    storage.appendEventLog(event);

    const auto loaded_user = storage.findUser(user.player_id);
    const auto records = storage.listAnswerRecords(user.player_id);
    const auto loaded_progress = storage.loadProgress(user.player_id, progress.scene_id);

    std::cout << "connected: true\n";
    std::cout << "user: " << (loaded_user ? loaded_user->display_name : "<missing>") << '\n';
    std::cout << "answer id: " << answer_result.id << '\n';
    std::cout << "answer records: " << records.size() << '\n';
    std::cout << "progress loaded: " << (loaded_progress ? "true" : "false") << '\n';
    std::cout << "event logs: " << storage.listEventLogs(10).size() << '\n';

    storage.disconnect();
    return 0;
}
