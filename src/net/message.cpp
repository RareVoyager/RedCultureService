#include "rcs/net/message.hpp"

#include <algorithm>

namespace rcs::net {

namespace {

// 线上的整数统一使用大端序，避免不同平台字节序造成协议不一致。
std::uint16_t read_u16_be(const std::uint8_t* data) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8) |
                                      static_cast<std::uint16_t>(data[1]));
}

std::uint32_t read_u32_be(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24) |
           (static_cast<std::uint32_t>(data[1]) << 16) |
           (static_cast<std::uint32_t>(data[2]) << 8) |
           static_cast<std::uint32_t>(data[3]);
}

void write_u16_be(std::uint16_t value, std::uint8_t* out) {
    out[0] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    out[1] = static_cast<std::uint8_t>(value & 0xff);
}

void write_u32_be(std::uint32_t value, std::uint8_t* out) {
    out[0] = static_cast<std::uint8_t>((value >> 24) & 0xff);
    out[1] = static_cast<std::uint8_t>((value >> 16) & 0xff);
    out[2] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    out[3] = static_cast<std::uint8_t>(value & 0xff);
}

} // namespace

ByteBuffer FrameCodec::encode(const Message& message) {
    if (message.payload.size() > default_max_payload_size) {
        throw std::length_error("message payload is too large");
    }

    // 分配一块连续缓冲区，方便 Boost.Asio 直接写出。
    ByteBuffer buffer(header_size + message.payload.size());
    const FrameHeader header{
        static_cast<std::uint32_t>(message.payload.size()),
        message.type,
        message.flags,
    };

    encode_header(header, buffer.data());
    std::copy(message.payload.begin(), message.payload.end(), buffer.begin() + header_size);
    return buffer;
}

std::optional<Message> FrameCodec::try_decode(ByteBuffer& buffer, std::size_t max_payload_size) {
    if (buffer.size() < header_size) {
        return std::nullopt;
    }

    // 半包数据继续留在 buffer 中，直到完整 payload 到达。
    const auto header = decode_header(buffer.data(), max_payload_size);
    const auto frame_size = header_size + static_cast<std::size_t>(header.payload_size);
    if (buffer.size() < frame_size) {
        return std::nullopt;
    }

    Message message;
    message.type = header.type;
    message.flags = header.flags;
    message.payload.assign(buffer.begin() + header_size, buffer.begin() + frame_size);
    buffer.erase(buffer.begin(), buffer.begin() + frame_size);
    return message;
}

FrameHeader FrameCodec::decode_header(const std::uint8_t* data, std::size_t max_payload_size) {
    const auto payload_size = read_u32_be(data);
    if (payload_size > max_payload_size) {
        // 在分配 body 缓冲区之前拒绝超大帧。
        throw std::length_error("frame payload exceeds configured limit");
    }

    return FrameHeader{
        payload_size,
        static_cast<MessageType>(read_u16_be(data + 4)),
        read_u16_be(data + 6),
    };
}

void FrameCodec::encode_header(const FrameHeader& header, std::uint8_t* out) {
    write_u32_be(header.payload_size, out);
    write_u16_be(static_cast<std::uint16_t>(header.type), out + 4);
    write_u16_be(header.flags, out + 6);
}

Message make_text_message(std::string text) {
    Message message;
    message.type = MessageType::text;
    message.payload.assign(text.begin(), text.end());
    return message;
}

std::string payload_as_string(const Message& message) {
    return std::string(message.payload.begin(), message.payload.end());
}

} // namespace rcs::net
