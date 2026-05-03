#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rcs::sync {

using RoomId = std::uint64_t;
using SnapshotVersion = std::uint64_t;

struct Vector3 {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct PlayerTransform {
    Vector3 position;
    float yaw{0.0F};
    float pitch{0.0F};
};

struct PlayerAction {
    // 业务动作名，例如 idle、move、jump、interact。
    std::string name{"idle"};

    // 动作序号由客户端递增，服务端用于识别重复或乱序动作。
    std::uint64_t sequence{0};
};

struct PlayerState {
    std::string player_id;
    std::uint64_t connection_id{0};
    PlayerTransform transform;
    PlayerAction action;
    std::uint64_t last_input_sequence{0};
    bool online{true};
    std::chrono::steady_clock::time_point updated_at{};
};

struct StateDelta {
    RoomId room_id{0};
    std::string player_id;
    PlayerTransform transform;
    PlayerAction action;
    std::uint64_t input_sequence{0};
    SnapshotVersion snapshot_version{0};
    std::chrono::steady_clock::time_point created_at{};
};

struct StateSnapshot {
    RoomId room_id{0};
    SnapshotVersion version{0};
    std::vector<PlayerState> players;
    std::chrono::steady_clock::time_point created_at{};
};

struct StateInput {
    RoomId room_id{0};
    std::string player_id;
    std::uint64_t input_sequence{0};
    PlayerTransform transform;
    PlayerAction action;
};

struct StateSubmitResult {
    bool ok{false};
    std::string error;
    std::optional<PlayerState> state;
    std::optional<StateDelta> delta;
};

struct SyncConfig {
    // 单次状态上报允许的最大位移，优先拦截明显瞬移。
    float max_single_update_distance{20.0F};

    // 按时间估算的最大移动速度，避免持续超速。
    float max_move_speed_per_second{12.0F};

    // 给网络抖动和浮点误差留一点余量。
    float movement_tolerance{0.5F};

    // 房间快照间隔，后续网络层可定时把快照广播给客户端。
    std::chrono::milliseconds snapshot_interval{100};

    // 每个房间最多保留多少条待广播增量。
    std::size_t max_pending_deltas_per_room{1024};
};

class StateSyncService {
public:
    explicit StateSyncService(SyncConfig config = {});

    const SyncConfig& config() const noexcept;

    bool registerRoom(RoomId room_id);
    bool unregisterRoom(RoomId room_id);

    StateSubmitResult addPlayer(RoomId room_id,
                                std::string player_id,
                                std::uint64_t connection_id,
                                PlayerTransform initial_transform = {});
    bool removePlayer(RoomId room_id, const std::string& player_id);
    bool markPlayerOffline(RoomId room_id, const std::string& player_id);

    // 提交客户端状态。通过校验后，服务端状态会被更新并生成一条增量。
    StateSubmitResult submitState(const StateInput& input);

    std::optional<PlayerState> findPlayerState(RoomId room_id, const std::string& player_id) const;
    std::optional<StateSnapshot> buildSnapshot(RoomId room_id) const;
    std::vector<StateSnapshot> buildDueSnapshots();

    // 取出并清空房间待广播增量。
    std::vector<StateDelta> drainDeltas(RoomId room_id);

    std::size_t roomCount() const;
    std::size_t playerCount(RoomId room_id) const;

private:
    struct RoomSyncState {
        SnapshotVersion version{0};
        std::unordered_map<std::string, PlayerState> players;
        std::vector<StateDelta> pending_deltas;
        std::chrono::steady_clock::time_point last_snapshot_at{};
    };

    StateSubmitResult reject(std::string error) const;
    StateSubmitResult accept(RoomSyncState& room, PlayerState& state, const StateInput& input);
    bool isMovementAllowed(const PlayerState& current,
                           const PlayerTransform& next,
                           std::chrono::steady_clock::time_point now) const;
    StateSnapshot buildSnapshotLocked(RoomId room_id, const RoomSyncState& room) const;

    SyncConfig config_;
    mutable std::mutex mutex_;
    std::unordered_map<RoomId, RoomSyncState> rooms_;
};

float distance(const Vector3& lhs, const Vector3& rhs);

} // namespace rcs::sync
