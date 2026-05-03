#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rcs::room {

using RoomId = std::uint64_t;
using MatchTicketId = std::uint64_t;

enum class RoomState {
    waiting = 0,
    playing = 1,
    closed = 2,
};

struct PlayerRef {
    // 鉴权模块确认后的玩家 id。
    std::string player_id;

    // 网络连接 id 由接入层传入；0 表示暂未绑定连接。
    std::uint64_t connection_id{0};
};

struct RoomMember {
    PlayerRef player;
    bool ready{false};
    std::chrono::steady_clock::time_point joined_at{};
};

struct RoomOptions {
    std::string mode{"default"};
    std::size_t max_players{4};

    // 满员后是否自动进入 playing 状态。
    bool auto_start_when_full{true};
};

struct RoomInfo {
    RoomId id{0};
    std::string mode;
    RoomState state{RoomState::waiting};
    std::size_t max_players{0};
    bool auto_start_when_full{true};
    std::vector<RoomMember> members;
    std::chrono::steady_clock::time_point created_at{};
    std::chrono::steady_clock::time_point updated_at{};
};

struct RoomResult {
    bool ok{false};
    std::string error;
    std::optional<RoomInfo> room;
};

struct MatchRequest {
    PlayerRef player;
    std::string mode{"default"};
    std::size_t preferred_room_size{4};

    // 超过该时间仍未匹配成功，tick() 会将票据标记为过期。
    std::chrono::milliseconds timeout{std::chrono::seconds(30)};
};

struct MatchTicket {
    MatchTicketId id{0};
    MatchRequest request;
    std::chrono::steady_clock::time_point created_at{};
};

struct MatchTicketResult {
    bool ok{false};
    std::string error;
    std::optional<MatchTicket> ticket;
};

struct MatchTickResult {
    std::vector<RoomInfo> created_rooms;
    std::vector<MatchTicket> expired_tickets;
};

class RoomMatchService {
public:
    RoomMatchService() = default;

    RoomResult createRoom(const PlayerRef& host, const RoomOptions& options = {});
    RoomResult joinRoom(RoomId room_id, const PlayerRef& player);
    RoomResult leaveRoom(RoomId room_id, const std::string& player_id);
    RoomResult closeRoom(RoomId room_id);
    RoomResult setReady(RoomId room_id, const std::string& player_id, bool ready);

    std::optional<RoomInfo> findRoom(RoomId room_id) const;
    std::optional<RoomInfo> findRoomByPlayer(const std::string& player_id) const;
    std::vector<RoomInfo> listRooms(bool include_closed = false) const;

    // 将玩家加入匹配队列。当前版本限制一个玩家只能持有一个匹配票据。
    MatchTicketResult enqueueMatch(const MatchRequest& request);
    bool cancelMatch(MatchTicketId ticket_id);
    bool cancelMatchByPlayer(const std::string& player_id);

    // 推进匹配队列：清理过期票据，并按模式和房间大小自动成房。
    MatchTickResult tick();

    std::size_t roomCount(bool include_closed = false) const;
    std::size_t waitingCount(std::optional<std::string> mode = std::nullopt) const;

private:
    RoomResult createRoomLocked(const PlayerRef& host, const RoomOptions& options);
    std::optional<RoomId> findActiveRoomIdByPlayerLocked(const std::string& player_id) const;
    void removePlayerTicketLocked(const std::string& player_id);
    void updateRoomStateLocked(RoomInfo& room);
    bool sameMatchBucket(const MatchTicket& lhs, const MatchTicket& rhs) const;

    mutable std::mutex mutex_;
    RoomId next_room_id_{1};
    MatchTicketId next_ticket_id_{1};
    std::unordered_map<RoomId, RoomInfo> rooms_;
    std::deque<MatchTicket> match_queue_;
    std::unordered_map<std::string, MatchTicketId> player_tickets_;
};

const char* toString(RoomState state);

} // namespace rcs::room
