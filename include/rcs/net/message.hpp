#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace rcs::net {

using ByteBuffer = std::vector<std::uint8_t>;

// 网络层只关心协议帧类型；更具体的业务消息由 payload 中的 JSON/Protobuf 决定。
enum class MessageType : std::uint16_t {
    unknown = 0,
    heartbeat = 1,
    text = 2,
    binary = 3,
};

struct Message {
    // 协议层消息类型。
    MessageType type{MessageType::unknown};

    // 预留给压缩、加密、版本等协议标记。
    std::uint16_t flags{0};

    // 原始负载字节，上层模块决定具体解析方式。
    ByteBuffer payload;
};

struct FrameHeader {
    std::uint32_t payload_size{0};
    MessageType type{MessageType::unknown};
    std::uint16_t flags{0};
};

// 固定帧格式：
// 4 字节 payload_size，大端序
// 2 字节 message_type，大端序
// 2 字节 flags，大端序
// N 字节 payload
class FrameCodec {
public:
    static constexpr std::size_t header_size = 8;
    static constexpr std::size_t default_max_payload_size = 1024 * 1024;

    // 将一条完整消息编码成可直接写入网络的字节帧。
    static ByteBuffer encode(const Message& message);

    // 从流式缓冲区中尝试解析一条完整消息；数据不足时返回 nullopt。
    static std::optional<Message> tryDecode(ByteBuffer& buffer,
                                            std::size_t max_payload_size = default_max_payload_size);

    // 只解析固定长度帧头，方便 TcpConnection 在分配 body 缓冲区前校验长度。
    static FrameHeader decodeHeader(const std::uint8_t* data,
                                    std::size_t max_payload_size = default_max_payload_size);

    static void encodeHeader(const FrameHeader& header, std::uint8_t* out);
};

Message makeTextMessage(std::string text);
std::string payloadAsString(const Message& message);

} // namespace rcs::net
