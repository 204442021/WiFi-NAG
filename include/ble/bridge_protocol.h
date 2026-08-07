#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace BleBridgeProtocol
{
static constexpr std::size_t kPacketSize = 20;
static constexpr std::size_t kPayloadOffset = 8;
static constexpr std::size_t kPayloadSize = 10;
static constexpr uint8_t kMagic = 0xA7;
static constexpr uint8_t kVersion = 0x02;

enum MessageType : uint8_t
{
    MSG_OBSTACLE_STATE = 0x01,
    MSG_SET_NAG = 0x10,
    MSG_QUERY_NAG = 0x11,
    MSG_NAG_STATE = 0x20,
    MSG_HELLO = 0x30,
    MSG_HELLO_ACK = 0x31,
};

enum DecodeError : uint8_t
{
    DECODE_OK = 0,
    DECODE_BAD_LENGTH,
    DECODE_BAD_MAGIC,
    DECODE_BAD_VERSION,
    DECODE_BAD_CRC,
};

struct Packet
{
    uint8_t bytes[kPacketSize] = {};
};

inline uint16_t crc16Ccitt(const uint8_t *data, std::size_t length)
{
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < length; ++i)
    {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000U) ? static_cast<uint16_t>((crc << 1) ^ 0x1021U)
                                  : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

inline uint16_t readLe16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t readLe32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline void writeLe16(uint8_t *p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value & 0xFFU);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

inline void writeLe32(uint8_t *p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value & 0xFFU);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
    p[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
    p[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
}

inline uint8_t *payload(Packet &packet)
{
    return packet.bytes + kPayloadOffset;
}

inline const uint8_t *payload(const Packet &packet)
{
    return packet.bytes + kPayloadOffset;
}

inline Packet makePacket(MessageType type, uint8_t flags, uint32_t sequence)
{
    Packet packet;
    packet.bytes[0] = kMagic;
    packet.bytes[1] = kVersion;
    packet.bytes[2] = static_cast<uint8_t>(type);
    packet.bytes[3] = flags;
    writeLe32(packet.bytes + 4, sequence);
    return packet;
}

inline void finalize(Packet &packet)
{
    writeLe16(packet.bytes + 18, crc16Ccitt(packet.bytes, 18));
}

inline DecodeError validate(const uint8_t *data, std::size_t length)
{
    if (!data || length != kPacketSize)
        return DECODE_BAD_LENGTH;
    if (data[0] != kMagic)
        return DECODE_BAD_MAGIC;
    if (data[1] != kVersion)
        return DECODE_BAD_VERSION;
    if (readLe16(data + 18) != crc16Ccitt(data, 18))
        return DECODE_BAD_CRC;
    return DECODE_OK;
}

inline bool isKnownMessageType(uint8_t type)
{
    switch (type)
    {
    case MSG_OBSTACLE_STATE:
    case MSG_SET_NAG:
    case MSG_QUERY_NAG:
    case MSG_NAG_STATE:
    case MSG_HELLO:
    case MSG_HELLO_ACK:
        return true;
    default:
        return false;
    }
}

inline bool copyPacket(Packet &out, const uint8_t *data, std::size_t length)
{
    if (validate(data, length) != DECODE_OK)
        return false;
    std::memcpy(out.bytes, data, kPacketSize);
    return true;
}
} // namespace BleBridgeProtocol
