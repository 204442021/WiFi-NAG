#pragma once

#include <cstdint>

#include "shared_types.h"

#if defined(ESP_PLATFORM) && defined(BLE_BRIDGE)
#include <esp_random.h>
#endif

#include "nag_state_controller.h"

inline Shared<bool> obstacleShiftFeatureEnabled{false};

namespace BleBridgeTiming
{
static constexpr uint32_t kObstacleStatePeriodMs = 50;
static constexpr uint32_t kBridgeServicePollMs = 20;

inline bool obstacleStateDue(uint32_t nowMs, uint32_t lastObstacleMs)
{
    return nowMs - lastObstacleMs >= kObstacleStatePeriodMs;
}

inline uint32_t nextObstacleServiceDelayMs(uint32_t nowMs,
                                           uint32_t lastObstacleMs,
                                           bool forwardingEnabled)
{
    if (!forwardingEnabled)
        return kBridgeServicePollMs;
    const uint32_t elapsed = nowMs - lastObstacleMs;
    if (elapsed >= kObstacleStatePeriodMs)
        return kBridgeServicePollMs;
    const uint32_t remaining = kObstacleStatePeriodMs - elapsed;
    return remaining < kBridgeServicePollMs ? remaining : kBridgeServicePollMs;
}
} // namespace BleBridgeTiming

enum BleBridgeProtocolStatus : uint8_t
{
    BLE_BRIDGE_PROTOCOL_DISABLED = 0,
    BLE_BRIDGE_PROTOCOL_IDLE,
    BLE_BRIDGE_PROTOCOL_SCANNING,
    BLE_BRIDGE_PROTOCOL_CONNECTING,
    BLE_BRIDGE_PROTOCOL_DISCOVERING,
    BLE_BRIDGE_PROTOCOL_HANDSHAKE,
    BLE_BRIDGE_PROTOCOL_READY,
    BLE_BRIDGE_PROTOCOL_INCOMPATIBLE,
    BLE_BRIDGE_PROTOCOL_ERROR,
};

struct BleBridgeDiagnostics
{
    bool enabled = false;
    bool obstacleForwarding = false;
    bool taskStarted = false;
    bool scanning = false;
    bool connecting = false;
    bool connected = false;
    bool bonded = false;
    bool subscribed = false;
    bool bridgeReady = false;
    bool pairing = false;
    uint32_t pairingRemainingMs = 0;
    bool unbindPending = false;
    uint32_t unbindPeerDeviceId = 0;
    uint32_t unbindTransactionId = 0;

    uint32_t deviceId = 0;
    uint32_t bootId = 0;
    uint32_t peerDeviceId = 0;
    uint32_t peerBootId = 0;
    uint8_t peerCapabilities = 0;
    int8_t rssi = -127;
    BleBridgeProtocolStatus protocolStatus = BLE_BRIDGE_PROTOCOL_DISABLED;
    uint16_t lastDisconnectReason = 0;

    bool hasLastPacket = false;
    uint32_t lastPacketAgeMs = 0;
    bool hasLastSend = false;
    uint32_t lastSendAgeMs = 0;

    bool fresh255 = false;
    bool fresh12B = false;
    bool partyCanAlive = false;
    bool dlc255Valid = false;
    bool dlc12BValid = false;
    uint32_t last255AgeMs = 0;
    uint32_t last12BAgeMs = 0;
    uint8_t suggestedGear = 0;
    uint8_t torqueDirection = 0;

    bool hasLastRemoteCommand = false;
    bool lastRemoteDesired = false;
    uint16_t lastRemoteCommandId = 0;
    NagResultCode lastCommandResult = NAG_RESULT_OK;

    uint32_t reconnectCount = 0;
    uint32_t disconnectCount = 0;
    uint32_t obstacleTxCount = 0;
    uint32_t obstacleTxFailCount = 0;
    uint32_t stateReportCount = 0;
    uint32_t crcFailCount = 0;
    uint32_t badLengthCount = 0;
    uint32_t badMagicCount = 0;
    uint32_t badVersionCount = 0;
    uint32_t unknownTypeCount = 0;
    uint32_t peerRejectCount = 0;
    uint32_t sequenceGapCount = 0;
    uint32_t duplicateOrOldSequenceCount = 0;
    uint32_t badBrakeStateCount = 0;
    bool obstacleTransportSupported = false;
    bool obstacleTransportValid = false;
    bool obstacleTransportPaused = false;
    uint8_t obstacleTransportGear = 0;
    uint8_t obstacleTransportReason = 0;
    uint32_t obstacleTransportStateGeneration = 0;
    uint32_t obstacleTransportLastRxAgeMs = 0;
    uint32_t obstacleTransportBadPayloadCount = 0;
    uint32_t obstacleTransportTimeoutCount = 0;
    uint32_t obstaclePausedCanFrameSkipCount = 0;
    uint32_t obstaclePausedTxSlotCount = 0;
};

class BleBridgeClient
{
public:
    void begin();
    void setEnabled(bool enabled, bool persist = true);
    bool enabled() const;
    void setObstacleForwarding(bool enabled, bool persist = true);
    bool obstacleForwarding() const;
    void setObstacleShiftEnabled(bool enabled, bool persist = true);
    bool obstacleShiftEnabled() const;
    bool startPairing();
    void unbind();
    void prepareForRestart();
    void forceNagState();
    bool obstacleTransportPaused() const;
    void noteObstacleCanFrameSkipped();
    BleBridgeDiagnostics diagnostics() const;
};

extern BleBridgeClient bleBridgeClient;
