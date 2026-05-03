#include "rcs/room/room_match_service.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace rcs::room {

namespace {

bool isValidPlayer(const PlayerRef& player) {
    return !player.player_id.empty();
}

bool isClosed(const RoomInfo& room) {
    return room.state == RoomState::closed;
}

} // namespace

RoomResult RoomMatchService::createRoom(const PlayerRef& host, const RoomOptions& options) {
    std::lock_guard<std::mutex> lock(mutex_);
    return createRoomLocked(host, options);
}

RoomResult RoomMatchService::joinRoom(RoomId room_id, const PlayerRef& player) {
    if (!isValidPlayer(player)) {
        return RoomResult{false, "player_id is empty", std::nullopt};
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return RoomResult{false, "room not found", std::nullopt};
    }

    auto& room = room_it->second;
    if (isClosed(room)) {
        return RoomResult{false, "room is closed", room};
    }

    const auto member_it = std::find_if(room.members.begin(), room.members.end(), [&](const RoomMember& member) {
        return member.player.player_id == player.player_id;
    });
    if (member_it != room.members.end()) {
        return RoomResult{true, {}, room};
    }

    const auto current_room = findActiveRoomIdByPlayerLocked(player.player_id);
    if (current_room && *current_room != room_id) {
        return RoomResult{false, "player already in another room", std::nullopt};
    }

    if (room.members.size() >= room.max_players) {
        return RoomResult{false, "room is full", room};
    }

    removePlayerTicketLocked(player.player_id);

    room.members.push_back(RoomMember{player, false, std::chrono::steady_clock::now()});
    updateRoomStateLocked(room);
    return RoomResult{true, {}, room};
}

RoomResult RoomMatchService::leaveRoom(RoomId room_id, const std::string& player_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return RoomResult{false, "room not found", std::nullopt};
    }

    auto& room = room_it->second;
    const auto before = room.members.size();
    room.members.erase(std::remove_if(room.members.begin(), room.members.end(), [&](const RoomMember& member) {
                           return member.player.player_id == player_id;
                       }),
                       room.members.end());

    if (room.members.size() == before) {
        return RoomResult{false, "player not in room", room};
    }

    if (room.members.empty()) {
        room.state = RoomState::closed;
    } else {
        updateRoomStateLocked(room);
    }
    room.updated_at = std::chrono::steady_clock::now();
    return RoomResult{true, {}, room};
}

RoomResult RoomMatchService::closeRoom(RoomId room_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return RoomResult{false, "room not found", std::nullopt};
    }

    auto& room = room_it->second;
    room.state = RoomState::closed;
    room.updated_at = std::chrono::steady_clock::now();
    return RoomResult{true, {}, room};
}

RoomResult RoomMatchService::setReady(RoomId room_id, const std::string& player_id, bool ready) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return RoomResult{false, "room not found", std::nullopt};
    }

    auto& room = room_it->second;
    if (isClosed(room)) {
        return RoomResult{false, "room is closed", room};
    }

    const auto member_it = std::find_if(room.members.begin(), room.members.end(), [&](RoomMember& member) {
        return member.player.player_id == player_id;
    });
    if (member_it == room.members.end()) {
        return RoomResult{false, "player not in room", room};
    }

    member_it->ready = ready;
    room.updated_at = std::chrono::steady_clock::now();
    return RoomResult{true, {}, room};
}

std::optional<RoomInfo> RoomMatchService::findRoom(RoomId room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<RoomInfo> RoomMatchService::findRoomByPlayer(const std::string& player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto room_id = findActiveRoomIdByPlayerLocked(player_id);
    if (!room_id) {
        return std::nullopt;
    }
    return rooms_.at(*room_id);
}

std::vector<RoomInfo> RoomMatchService::listRooms(bool include_closed) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<RoomInfo> rooms;
    rooms.reserve(rooms_.size());
    for (const auto& [_, room] : rooms_) {
        if (include_closed || !isClosed(room)) {
            rooms.push_back(room);
        }
    }
    return rooms;
}

MatchTicketResult RoomMatchService::enqueueMatch(const MatchRequest& request) {
    if (!isValidPlayer(request.player)) {
        return MatchTicketResult{false, "player_id is empty", std::nullopt};
    }
    if (request.preferred_room_size == 0) {
        return MatchTicketResult{false, "preferred_room_size must be greater than zero", std::nullopt};
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (findActiveRoomIdByPlayerLocked(request.player.player_id)) {
        return MatchTicketResult{false, "player already in room", std::nullopt};
    }
    if (player_tickets_.find(request.player.player_id) != player_tickets_.end()) {
        return MatchTicketResult{false, "player already in match queue", std::nullopt};
    }

    MatchTicket ticket;
    ticket.id = next_ticket_id_++;
    ticket.request = request;
    ticket.created_at = std::chrono::steady_clock::now();

    player_tickets_[request.player.player_id] = ticket.id;
    match_queue_.push_back(ticket);
    return MatchTicketResult{true, {}, ticket};
}

bool RoomMatchService::cancelMatch(MatchTicketId ticket_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = std::find_if(match_queue_.begin(), match_queue_.end(), [&](const MatchTicket& ticket) {
        return ticket.id == ticket_id;
    });
    if (it == match_queue_.end()) {
        return false;
    }

    player_tickets_.erase(it->request.player.player_id);
    match_queue_.erase(it);
    return true;
}

bool RoomMatchService::cancelMatchByPlayer(const std::string& player_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (player_tickets_.find(player_id) == player_tickets_.end()) {
        return false;
    }

    removePlayerTicketLocked(player_id);
    return true;
}

MatchTickResult RoomMatchService::tick() {
    std::lock_guard<std::mutex> lock(mutex_);

    MatchTickResult result;
    const auto now = std::chrono::steady_clock::now();

    for (auto it = match_queue_.begin(); it != match_queue_.end();) {
        if (now - it->created_at >= it->request.timeout) {
            result.expired_tickets.push_back(*it);
            player_tickets_.erase(it->request.player.player_id);
            it = match_queue_.erase(it);
        } else {
            ++it;
        }
    }

    bool created_room = true;
    while (created_room) {
        created_room = false;
        std::vector<MatchTicket> group;

        for (auto seed_it = match_queue_.begin(); seed_it != match_queue_.end(); ++seed_it) {
            group.clear();
            group.push_back(*seed_it);

            for (auto it = match_queue_.begin(); it != match_queue_.end() &&
                 group.size() < seed_it->request.preferred_room_size; ++it) {
                if (it->id != seed_it->id && sameMatchBucket(*seed_it, *it)) {
                    group.push_back(*it);
                }
            }

            if (group.size() >= seed_it->request.preferred_room_size) {
                break;
            }
        }

        if (group.empty() || group.size() < group.front().request.preferred_room_size) {
            break;
        }

        for (const auto& ticket : group) {
            player_tickets_.erase(ticket.request.player.player_id);
        }
        match_queue_.erase(std::remove_if(match_queue_.begin(), match_queue_.end(), [&](const MatchTicket& ticket) {
                               return std::any_of(group.begin(), group.end(), [&](const MatchTicket& matched) {
                                   return matched.id == ticket.id;
                               });
                           }),
                           match_queue_.end());

        RoomOptions options;
        options.mode = group.front().request.mode;
        options.max_players = group.front().request.preferred_room_size;
        options.auto_start_when_full = true;

        auto room_result = createRoomLocked(group.front().request.player, options);
        if (!room_result.ok || !room_result.room) {
            continue;
        }

        auto& stored_room = rooms_.at(room_result.room->id);
        for (std::size_t i = 1; i < group.size(); ++i) {
            stored_room.members.push_back(RoomMember{
                group[i].request.player,
                false,
                std::chrono::steady_clock::now(),
            });
        }
        updateRoomStateLocked(stored_room);
        result.created_rooms.push_back(stored_room);
        created_room = true;
    }

    return result;
}

std::size_t RoomMatchService::roomCount(bool include_closed) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<std::size_t>(std::count_if(rooms_.begin(), rooms_.end(), [&](const auto& item) {
        return include_closed || !isClosed(item.second);
    }));
}

std::size_t RoomMatchService::waitingCount(std::optional<std::string> mode) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<std::size_t>(std::count_if(match_queue_.begin(), match_queue_.end(), [&](const MatchTicket& ticket) {
        return !mode || ticket.request.mode == *mode;
    }));
}

RoomResult RoomMatchService::createRoomLocked(const PlayerRef& host, const RoomOptions& options) {
    if (!isValidPlayer(host)) {
        return RoomResult{false, "player_id is empty", std::nullopt};
    }
    if (options.max_players == 0) {
        return RoomResult{false, "max_players must be greater than zero", std::nullopt};
    }
    if (findActiveRoomIdByPlayerLocked(host.player_id)) {
        return RoomResult{false, "player already in room", std::nullopt};
    }

    removePlayerTicketLocked(host.player_id);

    const auto now = std::chrono::steady_clock::now();

    RoomInfo room;
    room.id = next_room_id_++;
    room.mode = options.mode;
    room.state = RoomState::waiting;
    room.max_players = options.max_players;
    room.auto_start_when_full = options.auto_start_when_full;
    room.created_at = now;
    room.updated_at = now;
    room.members.push_back(RoomMember{host, false, now});
    updateRoomStateLocked(room);

    rooms_[room.id] = room;
    return RoomResult{true, {}, room};
}

std::optional<RoomId> RoomMatchService::findActiveRoomIdByPlayerLocked(const std::string& player_id) const {
    for (const auto& [room_id, room] : rooms_) {
        if (isClosed(room)) {
            continue;
        }

        const auto member_it = std::find_if(room.members.begin(), room.members.end(), [&](const RoomMember& member) {
            return member.player.player_id == player_id;
        });
        if (member_it != room.members.end()) {
            return room_id;
        }
    }
    return std::nullopt;
}

void RoomMatchService::removePlayerTicketLocked(const std::string& player_id) {
    const auto ticket_it = player_tickets_.find(player_id);
    if (ticket_it == player_tickets_.end()) {
        return;
    }

    const auto ticket_id = ticket_it->second;
    match_queue_.erase(std::remove_if(match_queue_.begin(), match_queue_.end(), [&](const MatchTicket& ticket) {
                           return ticket.id == ticket_id;
                       }),
                       match_queue_.end());
    player_tickets_.erase(ticket_it);
}

void RoomMatchService::updateRoomStateLocked(RoomInfo& room) {
    if (room.members.empty()) {
        room.state = RoomState::closed;
    } else if (room.auto_start_when_full && room.members.size() >= room.max_players) {
        room.state = RoomState::playing;
    } else if (room.state != RoomState::playing) {
        room.state = RoomState::waiting;
    }

    room.updated_at = std::chrono::steady_clock::now();
}

bool RoomMatchService::sameMatchBucket(const MatchTicket& lhs, const MatchTicket& rhs) const {
    return lhs.request.mode == rhs.request.mode &&
           lhs.request.preferred_room_size == rhs.request.preferred_room_size;
}

const char* toString(RoomState state) {
    switch (state) {
        case RoomState::waiting:
            return "waiting";
        case RoomState::playing:
            return "playing";
        case RoomState::closed:
            return "closed";
    }
    return "unknown";
}

} // namespace rcs::room
