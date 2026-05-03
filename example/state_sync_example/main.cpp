#include "rcs/sync/state_sync_service.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace {

void printSnapshot(const rcs::sync::StateSnapshot& snapshot) {
    std::cout << "snapshot room=" << snapshot.room_id
              << ", version=" << snapshot.version
              << ", players=" << snapshot.players.size()
              << '\n';

    for (const auto& player : snapshot.players) {
        std::cout << "  player=" << player.player_id
                  << ", pos=(" << player.transform.position.x
                  << ", " << player.transform.position.y
                  << ", " << player.transform.position.z << ")"
                  << ", action=" << player.action.name
                  << ", seq=" << player.last_input_sequence
                  << '\n';
    }
}

} // namespace

int main() {
    rcs::sync::SyncConfig config;
    config.max_move_speed_per_second = 20.0F;

    rcs::sync::StateSyncService sync(config);
    sync.registerRoom(1001);
    sync.addPlayer(1001, "player-1", 101);
    sync.addPlayer(1001, "player-2", 102);

    rcs::sync::StateInput input;
    input.room_id = 1001;
    input.player_id = "player-1";
    input.input_sequence = 1;
    input.transform.position = {0.1F, 0.0F, 0.0F};
    input.action = {"move", 1};

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto result = sync.submitState(input);
    if (!result.ok) {
        std::cout << "submit failed: " << result.error << '\n';
        return 1;
    }

    const auto deltas = sync.drainDeltas(1001);
    std::cout << "deltas: " << deltas.size() << '\n';

    auto snapshot = sync.buildSnapshot(1001);
    if (snapshot) {
        printSnapshot(*snapshot);
    }

    input.input_sequence = 2;
    input.transform.position = {999.0F, 0.0F, 0.0F};
    const auto rejected = sync.submitState(input);
    std::cout << "teleport accepted: " << (rejected.ok ? "true" : "false")
              << ", reason: " << rejected.error << '\n';

    return 0;
}
