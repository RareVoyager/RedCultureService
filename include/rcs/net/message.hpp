#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace rcs::net {

using ByteBuffer = std::vector<std::uint8_t>;

// 这里只保留协议层消息类型。后续业务消息 id 可以放进 payload，
// 再由 protobuf/json/二进制协议继续解析。
enum class MessageType : std::uint16_t {
    unknown = 0,
    heartbeat = 1,
    text = 2,
    binary = 3,
};

struct Message {
    // 协议层消息类别。
    MessageType type{MessageType::unknown};

    // 预留给压缩、加密、版本等标记。
    std::uint16_t flags{0};

    // 原始负载字节，上层模块决定具体解析方式。
    ByteBuffer payload;
};

struct FrameHeader {
    std::uint32_t payload_size{0};
    MessageType type{MessageType::unknown};
    std::uint16_t flags{0};
};

// 帧格式：
//   4 字节 payload_size，大端序
//   2 字节 message_type，大端序
//   2 字节 flags，大端序
//   N 字节 payload
class FrameCodec {
public:
    static constexpr std::size_t header_size = 8;
    static constexpr std::size_t default_max_payload_size = 1024 * 1024;

    // 将一条完整消息编码成可直接写入网络的帧。
    static ByteBuffer encode(const Message& message);

    // 尝试从流式缓冲区中解出一条完整消息；如果数据还不够一帧，
    // 返回 nullopt 并保留缓冲区内容。
    static std::optional<Message> try_decode(ByteBuffer& buffer,
                                             std::size_t max_payload_size = default_max_payload_size);

    // 只解析固定长度帧头。TcpConnection 会先用它校验长度，再分配 body 缓冲区。
    static FrameHeader decode_header(const std::uint8_t* data,
                                     std::size_t max_payload_size = default_max_payload_size);
    static void encode_header(const FrameHeader& header, std::uint8_t* out);
};

Message make_text_message(std::string text);
std::string payload_as_string(const Message& message);

} // namespace rcs::net
