#include "rcs/sync/state_sync_service.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rcs::sync {

StateSyncService::StateSyncService(SyncConfig config)
    : config_(std::move(config)) {}

const SyncConfig& StateSyncService::config() const noexcept {
    return config_;
}

bool StateSyncService::registerRoom(RoomId room_id) {
    if (room_id == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto [_, inserted] = rooms_.emplace(room_id, RoomSyncState{});
    return inserted;
}

bool StateSyncService::unregisterRoom(RoomId room_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return rooms_.erase(room_id) > 0;
}

StateSubmitResult StateSyncService::addPlayer(RoomId room_id,
                                               std::string player_id,
                                               std::uint64_t connection_id,
                                               PlayerTransform initial_transform) {
    if (room_id == 0) {
        return reject("room_id is zero");
    }
    if (player_id.empty()) {
        return reject("player_id is empty");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto& room = rooms_[room_id];

    PlayerState state;
    state.player_id = std::move(player_id);
    state.connection_id = connection_id;
    state.transform = initial_transform;
    state.updated_at = std::chrono::steady_clock::now();

    auto [it, inserted] = room.players.emplace(state.player_id, state);
    if (!inserted) {
        it->second.connection_id = connection_id;
        it->second.transform = initial_transform;
        it->second.online = true;
        it->second.updated_at = state.updated_at;
    }

    ++room.version;
    return StateSubmitResult{true, {}, it->second, std::nullopt};
}

bool StateSyncService::removePlayer(RoomId room_id, const std::string& player_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return false;
    }

    const auto removed = room_it->second.players.erase(player_id) > 0;
    if (removed) {
        ++room_it->second.version;
    }
    return removed;
}

bool StateSyncService::markPlayerOffline(RoomId room_id, const std::string& player_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return false;
    }

    const auto player_it = room_it->second.players.find(player_id);
    if (player_it == room_it->second.players.end()) {
        return false;
    }

    player_it->second.online = false;
    player_it->second.updated_at = std::chrono::steady_clock::now();
    ++room_it->second.version;
    return true;
}

StateSubmitResult StateSyncService::submitState(const StateInput& input) {
    if (input.room_id == 0) {
        return reject("room_id is zero");
    }
    if (input.player_id.empty()) {
        return reject("player_id is empty");
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const auto room_it = rooms_.find(input.room_id);
    if (room_it == rooms_.end()) {
        return reject("room not registered");
    }

    auto& room = room_it->second;
    const auto player_it = room.players.find(input.player_id);
    if (player_it == room.players.end()) {
        return reject("player not found in sync room");
    }

    auto& state = player_it->second;
    if (input.input_sequence <= state.last_input_sequence) {
        return reject("input sequence is out of order");
    }

    const auto now = std::chrono::steady_clock::now();
    if (!isMovementAllowed(state, input.transform, now)) {
        return reject("movement rejected by server authority");
    }

    return accept(room, state, input);
}

std::optional<PlayerState> StateSyncService::findPlayerState(RoomId room_id, const std::string& player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return std::nullopt;
    }

    const auto player_it = room_it->second.players.find(player_id);
    if (player_it == room_it->second.players.end()) {
        return std::nullopt;
    }

    return player_it->second;
}

std::optional<StateSnapshot> StateSyncService::buildSnapshot(RoomId room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return std::nullopt;
    }

    return buildSnapshotLocked(room_id, room_it->second);
}

std::vector<StateSnapshot> StateSyncService::buildDueSnapshots() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<StateSnapshot> snapshots;
    const auto now = std::chrono::steady_clock::now();

    for (auto& [room_id, room] : rooms_) {
        if (now - room.last_snapshot_at >= config_.snapshot_interval) {
            room.last_snapshot_at = now;
            snapshots.push_back(buildSnapshotLocked(room_id, room));
        }
    }

    return snapshots;
}

std::vector<StateDelta> StateSyncService::drainDeltas(RoomId room_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return {};
    }

    auto deltas = std::move(room_it->second.pending_deltas);
    room_it->second.pending_deltas.clear();
    return deltas;
}

std::size_t StateSyncService::roomCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rooms_.size();
}

std::size_t StateSyncService::playerCount(RoomId room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return 0;
    }

    return room_it->second.players.size();
}

StateSubmitResult StateSyncService::reject(std::string error) const {
    return StateSubmitResult{false, std::move(error), std::nullopt, std::nullopt};
}

StateSubmitResult StateSyncService::accept(RoomSyncState& room, PlayerState& state, const StateInput& input) {
    const auto now = std::chrono::steady_clock::now();

    state.transform = input.transform;
    state.action = input.action;
    state.last_input_sequence = input.input_sequence;
    state.online = true;
    state.updated_at = now;

    ++room.version;

    StateDelta delta;
    delta.room_id = input.room_id;
    delta.player_id = input.player_id;
    delta.transform = input.transform;
    delta.action = input.action;
    delta.input_sequence = input.input_sequence;
    delta.snapshot_version = room.version;
    delta.created_at = now;

    room.pending_deltas.push_back(delta);
    if (room.pending_deltas.size() > config_.max_pending_deltas_per_room) {
        room.pending_deltas.erase(room.pending_deltas.begin(),
                                  room.pending_deltas.begin() +
                                      static_cast<std::ptrdiff_t>(room.pending_deltas.size() -
                                                                  config_.max_pending_deltas_per_room));
    }

    return StateSubmitResult{true, {}, state, delta};
}

bool StateSyncService::isMovementAllowed(const PlayerState& current,
                                           const PlayerTransform& next,
                                           std::chrono::steady_clock::time_point now) const {
    const auto moved = distance(current.transform.position, next.position);
    if (moved > config_.max_single_update_distance + config_.movement_tolerance) {
        return false;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - current.updated_at);
    if (elapsed.count() <= 0) {
        return moved <= config_.movement_tolerance;
    }

    const auto seconds = static_cast<float>(elapsed.count()) / 1000.0F;
    const auto allowed = config_.max_move_speed_per_second * seconds + config_.movement_tolerance;
    return moved <= allowed;
}

StateSnapshot StateSyncService::buildSnapshotLocked(RoomId room_id, const RoomSyncState& room) const {
    StateSnapshot snapshot;
    snapshot.room_id = room_id;
    snapshot.version = room.version;
    snapshot.created_at = std::chrono::steady_clock::now();
    snapshot.players.reserve(room.players.size());

    for (const auto& [_, state] : room.players) {
        snapshot.players.push_back(state);
    }

    std::sort(snapshot.players.begin(), snapshot.players.end(), [](const PlayerState& lhs, const PlayerState& rhs) {
        return lhs.player_id < rhs.player_id;
    });
    return snapshot;
}

float distance(const Vector3& lhs, const Vector3& rhs) {
    const auto dx = lhs.x - rhs.x;
    const auto dy = lhs.y - rhs.y;
    const auto dz = lhs.z - rhs.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace rcs::sync
