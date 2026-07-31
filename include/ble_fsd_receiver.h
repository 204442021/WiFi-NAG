#pragma once

#include <cstddef>
#include <cstdint>

/*
 * BLE FSD status receiver.
 *
 * This module is deliberately limited to BLE validation and a visible,
 * time-bounded diagnostic window.  It has no CanDriver dependency and never
 * constructs, modifies, or submits a CAN frame.
 */

static constexpr uint8_t kBleFsdPacketMagic = 0xA5;
static constexpr uint8_t kBleFsdProtocolVersion = 0x01;
static constexpr uint8_t kBleFsdCommandActive = 0x01;
static constexpr size_t kBleFsdPacketSize = 16;

enum class BleFsdRejectReason : uint8_t
{
    None = 0,
    Disabled,
    InvalidLength,
    InvalidMagic,
    InvalidVersion,
    InvalidCommand,
    InvalidState,
    InvalidCrc,
    DuplicateSequence,
    OldSequence,
    InvalidTimestamp,
    CanUnhealthy,
    AwaitingClear,
};

enum class BleFsdReceiverState : uint8_t
{
    Disabled = 0,
    Idle,
    TestActive,
    AwaitingClear,
};

struct BleFsdPacket
{
    uint8_t command = 0;
    bool active = false;
    uint32_t sequence = 0;
    uint32_t sourceTimestampMs = 0;
    uint16_t holdMs = 0;
};

struct BleFsdReceiverConfig
{
    bool enabled = false;
    char peerMac[18] = ""; // Canonical AA:BB:CC:DD:EE:FF, mandatory whitelist.
    int8_t rssiThreshold = -90;
    uint32_t testWindowMs = 10000;
};

struct BleFsdReceiverStatus
{
    bool initialized = false;
    bool scanning = false;
    bool discoveryActive = false;
    bool connected = false;
    bool subscribed = false;
    bool remoteActive = false;
    int8_t rssi = 0;
    uint8_t peerAddressType = 0;
    BleFsdReceiverState state = BleFsdReceiverState::Disabled;
    BleFsdRejectReason lastReject = BleFsdRejectReason::None;
    uint32_t lastSequence = 0;
    uint32_t lastSourceTimestampMs = 0;
    uint32_t lastPacketAtMs = 0;
    uint32_t connectedAtMs = 0;
    uint32_t lastDisconnectAtMs = 0;
    uint32_t testRemainingMs = 0;
    uint32_t acceptedPackets = 0;
    uint32_t testWindows = 0;
    uint32_t crcErrors = 0;
    uint32_t timeoutCount = 0;
    uint32_t disconnectCount = 0;
    uint32_t duplicateCount = 0;
    uint32_t rejectedCount = 0;
    char peerMac[18] = "";
    char peerName[32] = "";
    char lastReason[24] = "disabled";
};

struct BleFsdScanEntry
{
    char mac[18] = "";
    char name[32] = "";
    int8_t rssi = 0;
    bool fsdServiceAdvertised = false;
    bool connectable = false;
    bool connected = false;
    bool saved = false;
};

inline uint16_t bleFsdCrc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

inline uint16_t bleFsdReadLe16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t bleFsdReadLe32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline bool bleFsdParsePacket(const uint8_t *data, size_t length,
                              BleFsdPacket &out, BleFsdRejectReason &reason)
{
    reason = BleFsdRejectReason::None;
    if (!data || length != kBleFsdPacketSize)
    {
        reason = BleFsdRejectReason::InvalidLength;
        return false;
    }
    if (data[0] != kBleFsdPacketMagic)
    {
        reason = BleFsdRejectReason::InvalidMagic;
        return false;
    }
    if (data[1] != kBleFsdProtocolVersion)
    {
        reason = BleFsdRejectReason::InvalidVersion;
        return false;
    }
    if (data[2] != kBleFsdCommandActive)
    {
        reason = BleFsdRejectReason::InvalidCommand;
        return false;
    }
    if (data[3] > 1)
    {
        reason = BleFsdRejectReason::InvalidState;
        return false;
    }
    if (bleFsdCrc16(data, 14) != bleFsdReadLe16(data + 14))
    {
        reason = BleFsdRejectReason::InvalidCrc;
        return false;
    }

    out.command = data[2];
    out.active = data[3] == 1;
    out.sequence = bleFsdReadLe32(data + 4);
    out.sourceTimestampMs = bleFsdReadLe32(data + 8);
    out.holdMs = bleFsdReadLe16(data + 12);
    return true;
}

const char *bleFsdReceiverStateName(BleFsdReceiverState state);
const char *bleFsdRejectReasonName(BleFsdRejectReason reason);

void bleFsdReceiverStart(const BleFsdReceiverConfig &config);
void bleFsdReceiverConfigure(const BleFsdReceiverConfig &config);
void bleFsdReceiverTick(bool canHealthy);
BleFsdReceiverStatus bleFsdReceiverGetStatus();
bool bleFsdReceiverStartDiscovery(uint32_t durationMs = 10000);
size_t bleFsdReceiverGetScanResults(BleFsdScanEntry *out, size_t capacity);
