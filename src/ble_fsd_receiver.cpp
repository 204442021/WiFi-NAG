#include "ble_fsd_receiver.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include <esp_timer.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/util/util.h>
#include <os/os_mbuf.h>
#include <services/gap/ble_svc_gap.h>
#endif

namespace
{
BleFsdReceiverConfig gConfig{};
BleFsdReceiverStatus gStatus{};
bool gCanHealthy = false;
bool gHaveSequence = false;
bool gHaveSourceTimestamp = false;
uint32_t gTestEndsAtMs = 0;
bool gDiscoveryMode = false;
uint32_t gDiscoveryEndsAtMs = 0;
bool gHostSynced = false;
bool gDiscoveryStartPending = false;
static constexpr size_t kMaxScanResults = 10;
BleFsdScanEntry gScanResults[kMaxScanResults] = {};
size_t gScanResultCount = 0;

bool textEqualsIgnoreCase(const char *a, const char *b)
{
    if (!a || !b)
        return false;
    while (*a && *b)
    {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'a' && ca <= 'z')
            ca = static_cast<char>(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z')
            cb = static_cast<char>(cb - 'a' + 'A');
        if (ca != cb)
            return false;
    }
    return *a == '\0' && *b == '\0';
}

void seedConfiguredPeerScanEntry()
{
    if (!gConfig.peerMac[0] || gScanResultCount >= kMaxScanResults)
        return;

    BleFsdScanEntry &entry = gScanResults[gScanResultCount++];
    std::snprintf(entry.mac, sizeof(entry.mac), "%s", gConfig.peerMac);
    if (textEqualsIgnoreCase(gStatus.peerMac, gConfig.peerMac))
    {
        std::snprintf(entry.name, sizeof(entry.name), "%s", gStatus.peerName);
        entry.rssi = gStatus.rssi;
        entry.connected = gStatus.connected;
        entry.connectable = gStatus.connected;
        entry.fsdServiceAdvertised = gStatus.subscribed;
    }
    entry.saved = true;
}

uint32_t nowMs()
{
#ifdef ESP_PLATFORM
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
#else
    return 0;
#endif
}

void setReason(const char *reason)
{
    std::snprintf(gStatus.lastReason, sizeof(gStatus.lastReason), "%s",
                  reason ? reason : "");
}

bool sequenceIsOlder(uint32_t candidate, uint32_t reference)
{
    return static_cast<int32_t>(candidate - reference) < 0;
}

void endTest(const char *reason, bool awaitClear)
{
    if (gStatus.state == BleFsdReceiverState::TestActive)
        gStatus.timeoutCount++;
    gTestEndsAtMs = 0;
    gStatus.testRemainingMs = 0;
    gStatus.state = awaitClear ? BleFsdReceiverState::AwaitingClear
                                : BleFsdReceiverState::Idle;
    setReason(reason);
}

void reject(BleFsdRejectReason reason)
{
    gStatus.lastReject = reason;
    gStatus.rejectedCount++;
    if (reason == BleFsdRejectReason::InvalidCrc)
        gStatus.crcErrors++;
    if (reason == BleFsdRejectReason::DuplicateSequence)
        gStatus.duplicateCount++;
}

void acceptPacket(const BleFsdPacket &packet)
{
    const uint32_t now = nowMs();

    if (packet.sourceTimestampMs == 0)
    {
        reject(BleFsdRejectReason::InvalidTimestamp);
        return;
    }
    if (gHaveSequence)
    {
        if (packet.sequence == gStatus.lastSequence)
        {
            /*
             * LILYGO repeats the same FSD_ACTIVE sequence every 500 ms during
             * its hold window.  These are heartbeats, not new trigger edges:
             * count them for diagnostics but never extend or retrigger the
             * diagnostic window.  A matching clear is also accepted so a
             * sender may use the same sequence for its final ACTIVE=0 packet.
             */
            gStatus.duplicateCount++;
            gStatus.lastPacketAtMs = now;
            gStatus.lastReject = BleFsdRejectReason::None;
            if (packet.active && gStatus.remoteActive)
                return;
            if (!packet.active && gStatus.remoteActive)
            {
                gStatus.remoteActive = false;
                if (gStatus.state == BleFsdReceiverState::AwaitingClear)
                    gStatus.state = BleFsdReceiverState::Idle;
                setReason("remote_clear");
                return;
            }
            reject(BleFsdRejectReason::DuplicateSequence);
            return;
        }
        if (sequenceIsOlder(packet.sequence, gStatus.lastSequence))
        {
            reject(BleFsdRejectReason::OldSequence);
            return;
        }
    }
    if (gHaveSourceTimestamp && sequenceIsOlder(packet.sourceTimestampMs, gStatus.lastSourceTimestampMs))
    {
        reject(BleFsdRejectReason::InvalidTimestamp);
        return;
    }

    const bool wasRemoteActive = gStatus.remoteActive;
    gHaveSequence = true;
    gHaveSourceTimestamp = true;
    gStatus.lastSequence = packet.sequence;
    gStatus.lastSourceTimestampMs = packet.sourceTimestampMs;
    gStatus.lastPacketAtMs = now;
    gStatus.acceptedPackets++;
    gStatus.lastReject = BleFsdRejectReason::None;

    if (!packet.active)
    {
        gStatus.remoteActive = false;
        if (gStatus.state == BleFsdReceiverState::AwaitingClear)
            gStatus.state = BleFsdReceiverState::Idle;
        setReason("remote_clear");
        return;
    }

    gStatus.remoteActive = true;
    if (wasRemoteActive || gStatus.state == BleFsdReceiverState::AwaitingClear)
    {
        if (gStatus.state == BleFsdReceiverState::AwaitingClear)
            reject(BleFsdRejectReason::AwaitingClear);
        return;
    }
    if (!gCanHealthy)
    {
        reject(BleFsdRejectReason::CanUnhealthy);
        return;
    }

    // Diagnostic-only window: deliberately not connected to the CAN TX path.
    const uint32_t windowMs = std::clamp(gConfig.testWindowMs, 1000UL, 60000UL);
    gTestEndsAtMs = now + windowMs;
    gStatus.testRemainingMs = windowMs;
    gStatus.testWindows++;
    gStatus.state = BleFsdReceiverState::TestActive;
    setReason("test_active");
}

#ifdef ESP_PLATFORM
static uint8_t gOwnAddrType = BLE_OWN_ADDR_PUBLIC;
static uint16_t gConnHandle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t gServiceStart = 0;
static uint16_t gServiceEnd = 0;
static uint16_t gCommandValueHandle = 0;
static bool gServiceFound = false;
static bool gCharacteristicFound = false;
static bool gCccdFound = false;

static const ble_uuid128_t kFsdServiceUuid =
    BLE_UUID128_INIT(0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd8, 0x91,
                     0x8d, 0x4a, 0x8f, 0x7b, 0x01, 0xf0, 0x30, 0x7a);
static const ble_uuid128_t kFsdCommandUuid =
    BLE_UUID128_INIT(0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd8, 0x91,
                     0x8d, 0x4a, 0x8f, 0x7b, 0x02, 0xf0, 0x30, 0x7a);

bool parsePeerMac(uint8_t out[6])
{
    unsigned int parsed[6] = {};
    if (std::sscanf(gConfig.peerMac, "%02x:%02x:%02x:%02x:%02x:%02x",
                    &parsed[0], &parsed[1], &parsed[2], &parsed[3], &parsed[4], &parsed[5]) != 6)
        return false;
    for (size_t i = 0; i < 6; ++i)
    {
        if (parsed[i] > 0xFF)
            return false;
        out[5 - i] = static_cast<uint8_t>(parsed[i]);
    }
    return true;
}

bool isWhitelisted(const ble_addr_t &address)
{
    uint8_t expected[6] = {};
    return parsePeerMac(expected) && std::memcmp(expected, address.val, sizeof(expected)) == 0;
}

void startScan();
int gapEvent(struct ble_gap_event *event, void *arg);

void addressToText(const ble_addr_t &address, char out[18])
{
    std::snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                  address.val[5], address.val[4], address.val[3],
                  address.val[2], address.val[1], address.val[0]);
}

void updatePeerFromAdvertisement(const struct ble_gap_disc_desc &disc)
{
    addressToText(disc.addr, gStatus.peerMac);
    gStatus.peerAddressType = disc.addr.type;
    gStatus.rssi = disc.rssi;

    struct ble_hs_adv_fields fields = {};
    if (ble_hs_adv_parse_fields(&fields, disc.data, disc.length_data) == 0 &&
        fields.name && fields.name_len > 0)
    {
        const size_t nameLen =
            std::min<size_t>(fields.name_len, sizeof(gStatus.peerName) - 1);
        std::memcpy(gStatus.peerName, fields.name, nameLen);
        gStatus.peerName[nameLen] = '\0';
    }
    else
    {
        gStatus.peerName[0] = '\0';
    }
}

void terminateAndScan(const char *reason)
{
    setReason(reason);
    gStatus.subscribed = false;
    if (gConnHandle != BLE_HS_CONN_HANDLE_NONE)
        ble_gap_terminate(gConnHandle, BLE_ERR_REM_USER_CONN_TERM);
    else
        startScan();
}

int onSubscribe(uint16_t, const struct ble_gatt_error *error,
                struct ble_gatt_attr *, void *)
{
    if (error->status == 0)
    {
        gStatus.subscribed = true;
        setReason("subscribed");
    }
    else
    {
        terminateAndScan("subscribe_failed");
    }
    return 0;
}

int onDescriptor(uint16_t connHandle, const struct ble_gatt_error *error,
                 uint16_t, const struct ble_gatt_dsc *descriptor, void *)
{
    if (error->status == 0)
    {
        if (ble_uuid_u16(&descriptor->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16)
        {
            gCccdFound = true;
            const uint8_t enableNotify[] = {0x01, 0x00};
            if (ble_gattc_write_flat(connHandle, descriptor->handle, enableNotify,
                                     sizeof(enableNotify), onSubscribe, nullptr) != 0)
                terminateAndScan("subscribe_failed");
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE)
    {
        if (!gCccdFound)
            terminateAndScan("cccd_missing");
        return 0;
    }
    terminateAndScan("descriptor_failed");
    return 0;
}

int onCharacteristic(uint16_t connHandle, const struct ble_gatt_error *error,
                     const struct ble_gatt_chr *characteristic, void *)
{
    if (error->status == 0)
    {
        gCharacteristicFound = true;
        gCommandValueHandle = characteristic->val_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE)
    {
        if (!gCharacteristicFound)
        {
            terminateAndScan("char_missing");
            return 0;
        }
        if (ble_gattc_disc_all_dscs(connHandle, gCommandValueHandle, gServiceEnd,
                                    onDescriptor, nullptr) != 0)
            terminateAndScan("descriptor_failed");
        return 0;
    }
    terminateAndScan("char_failed");
    return 0;
}

int onService(uint16_t connHandle, const struct ble_gatt_error *error,
              const struct ble_gatt_svc *service, void *)
{
    if (error->status == 0)
    {
        gServiceFound = true;
        gServiceStart = service->start_handle;
        gServiceEnd = service->end_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE)
    {
        if (!gServiceFound)
        {
            terminateAndScan("service_missing");
            return 0;
        }
        if (ble_gattc_disc_chrs_by_uuid(connHandle, gServiceStart, gServiceEnd,
                                        &kFsdCommandUuid.u, onCharacteristic, nullptr) != 0)
            terminateAndScan("char_failed");
        return 0;
    }
    terminateAndScan("service_failed");
    return 0;
}

void startScan()
{
    if (!gHostSynced ||
        (!gConfig.enabled && !gDiscoveryMode) ||
        (gStatus.connected && !gDiscoveryMode) ||
        gStatus.scanning)
        return;
    if (!gDiscoveryMode)
    {
        uint8_t expected[6] = {};
        if (!parsePeerMac(expected))
        {
            setReason("mac_required");
            return;
        }
    }
    struct ble_gap_disc_params params = {};
    // WiFi AP and BLE share one 2.4 GHz radio. NimBLE's zero defaults are
    // 30 ms interval / 30 ms window (100% scan duty), which can prevent WiFi
    // clients from associating. Keep normal background scanning below 20%.
    params.itvl = BLE_GAP_SCAN_ITVL_MS(gDiscoveryMode ? 100 : 160);
    params.window = BLE_GAP_SCAN_WIN_MS(gDiscoveryMode ? 40 : 30);
    params.filter_duplicates = gDiscoveryMode ? 0 : 1;
    params.passive = 1;
    int32_t durationMs = BLE_HS_FOREVER;
    if (gDiscoveryMode)
    {
        const uint32_t now = nowMs();
        if (static_cast<int32_t>(now - gDiscoveryEndsAtMs) >= 0)
        {
            gDiscoveryMode = false;
            gDiscoveryStartPending = false;
            setReason("discovery_done");
            startScan();
            return;
        }
        durationMs = static_cast<int32_t>(gDiscoveryEndsAtMs - now);
    }
    if (ble_gap_disc(gOwnAddrType, durationMs, &params, gapEvent, nullptr) == 0)
    {
        gStatus.scanning = true;
        setReason(gDiscoveryMode ? "discovery" : "scanning");
    }
    else
    {
        setReason("scan_failed");
    }
}

bool advertisesFsdService(const struct ble_gap_disc_desc &disc)
{
    struct ble_hs_adv_fields fields = {};
    if (ble_hs_adv_parse_fields(&fields, disc.data, disc.length_data) != 0)
        return false;
    for (int i = 0; i < fields.num_uuids128; ++i)
    {
        if (ble_uuid_cmp(&fields.uuids128[i].u, &kFsdServiceUuid.u) == 0)
            return true;
    }
    return false;
}

void recordAdvertisement(const struct ble_gap_disc_desc &disc)
{
    char mac[18] = {};
    addressToText(disc.addr, mac);

    size_t index = gScanResultCount;
    for (size_t i = 0; i < gScanResultCount; ++i)
    {
        if (textEqualsIgnoreCase(gScanResults[i].mac, mac))
        {
            index = i;
            break;
        }
    }
    if (index == gScanResultCount)
    {
        if (gScanResultCount >= kMaxScanResults)
            return;
        gScanResultCount++;
    }

    BleFsdScanEntry &entry = gScanResults[index];
    std::snprintf(entry.mac, sizeof(entry.mac), "%s", mac);
    entry.rssi = disc.rssi;
    entry.connectable = disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
                        disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
    entry.fsdServiceAdvertised = advertisesFsdService(disc);
    entry.saved = textEqualsIgnoreCase(mac, gConfig.peerMac);
    entry.connected = gStatus.connected &&
                      textEqualsIgnoreCase(mac, gStatus.peerMac);

    struct ble_hs_adv_fields fields = {};
    if (ble_hs_adv_parse_fields(&fields, disc.data, disc.length_data) == 0 &&
        fields.name && fields.name_len > 0)
    {
        const size_t nameLen = std::min<size_t>(fields.name_len, sizeof(entry.name) - 1);
        std::memcpy(entry.name, fields.name, nameLen);
        entry.name[nameLen] = '\0';
    }
    else
    {
        entry.name[0] = '\0';
    }
}

int gapEvent(struct ble_gap_event *event, void *)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
    {
        if (gDiscoveryMode)
        {
            recordAdvertisement(event->disc);
            return 0;
        }
        if (!gConfig.enabled || !isWhitelisted(event->disc.addr))
            return 0;
        if (event->disc.rssi < gConfig.rssiThreshold)
            return 0;
        const bool connectable =
            event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
            event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
        if (!connectable)
        {
            setReason("not_connectable");
            return 0;
        }
        /*
         * MAC is the trust boundary.  Do not require the 128-bit service UUID
         * to fit in the advertising packet: many ESP32 peripheral examples
         * expose the service over GATT but advertise only a name or
         * manufacturer payload.  The connection still performs mandatory
         * service / characteristic / CCCD discovery before any packet is
         * accepted.
         */
        if (ble_gap_disc_cancel() == 0 &&
            ble_gap_connect(gOwnAddrType, &event->disc.addr, 10000, nullptr, gapEvent, nullptr) == 0)
        {
            updatePeerFromAdvertisement(event->disc);
            gStatus.scanning = false;
            setReason("connecting");
        }
        return 0;
    }

    case BLE_GAP_EVENT_CONNECT:
        gStatus.scanning = false;
        if (event->connect.status != 0)
        {
            setReason("connect_failed");
            startScan();
            return 0;
        }
        gConnHandle = event->connect.conn_handle;
        gStatus.connected = true;
        gStatus.subscribed = false;
        gStatus.connectedAtMs = nowMs();
        gServiceFound = false;
        gCharacteristicFound = false;
        gCccdFound = false;
        if (ble_gattc_disc_svc_by_uuid(gConnHandle, &kFsdServiceUuid.u, onService, nullptr) != 0)
            terminateAndScan("service_failed");
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
    {
        if (event->notify_rx.attr_handle != gCommandValueHandle)
            return 0;
        uint8_t data[kBleFsdPacketSize] = {};
        const uint16_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (length > sizeof(data) ||
            os_mbuf_copydata(event->notify_rx.om, 0, length, data) != 0)
        {
            reject(BleFsdRejectReason::InvalidLength);
            return 0;
        }
        BleFsdPacket packet{};
        BleFsdRejectReason reason = BleFsdRejectReason::None;
        if (!bleFsdParsePacket(data, length, packet, reason))
            reject(reason);
        else
            acceptPacket(packet);
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT:
        gConnHandle = BLE_HS_CONN_HANDLE_NONE;
        gStatus.connected = false;
        gStatus.subscribed = false;
        gStatus.lastDisconnectAtMs = nowMs();
        gStatus.disconnectCount++;
        gStatus.remoteActive = false;
        gHaveSourceTimestamp = false;
        if (gStatus.state == BleFsdReceiverState::TestActive)
            endTest("link_lost", false);
        else if (!gDiscoveryMode)
            setReason("disconnected");
        if (gDiscoveryMode)
        {
            // A manual discovery request deliberately tears down the current
            // single BLE link, then starts its bounded scan from this callback.
            startScan();
            return 0;
        }
        startScan();
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        gStatus.scanning = false;
        if (gDiscoveryMode && gDiscoveryStartPending)
        {
            gDiscoveryStartPending = false;
            startScan();
            return 0;
        }
        if (gDiscoveryMode)
        {
            gDiscoveryMode = false;
            setReason("discovery_done");
            if (!gStatus.connected)
                startScan();
            return 0;
        }
        startScan();
        return 0;

    default:
        return 0;
    }
}

void onReset(int)
{
    gHostSynced = false;
    gStatus.connected = false;
    gStatus.subscribed = false;
    gStatus.scanning = false;
    gConnHandle = BLE_HS_CONN_HANDLE_NONE;
    gStatus.remoteActive = false;
    if (gStatus.state == BleFsdReceiverState::TestActive)
        endTest("ble_reset", false);
}

void onSync()
{
    if (ble_hs_util_ensure_addr(0) != 0 ||
        ble_hs_id_infer_auto(0, &gOwnAddrType) != 0)
    {
        setReason("addr_failed");
        return;
    }
    gHostSynced = true;
    startScan();
}

void hostTask(void *)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}
#endif

bool ensureBleInitialized()
{
    if (gStatus.initialized)
        return true;
#ifdef ESP_PLATFORM
    if (nimble_port_init() != 0)
    {
        setReason("init_failed");
        return false;
    }
    ble_hs_cfg.sync_cb = onSync;
    ble_hs_cfg.reset_cb = onReset;
    ble_svc_gap_device_name_set("WIFI-NAG BLE RX");
    gStatus.initialized = true;
    nimble_port_freertos_init(hostTask);
#else
    gStatus.initialized = true;
    gHostSynced = true;
#endif
    return true;
}
} // namespace

const char *bleFsdReceiverStateName(BleFsdReceiverState state)
{
    switch (state)
    {
    case BleFsdReceiverState::Disabled: return "DISABLED";
    case BleFsdReceiverState::Idle: return "IDLE";
    case BleFsdReceiverState::TestActive: return "TEST_ACTIVE";
    case BleFsdReceiverState::AwaitingClear: return "AWAIT_CLEAR";
    default: return "UNKNOWN";
    }
}

const char *bleFsdRejectReasonName(BleFsdRejectReason reason)
{
    switch (reason)
    {
    case BleFsdRejectReason::None: return "none";
    case BleFsdRejectReason::Disabled: return "disabled";
    case BleFsdRejectReason::InvalidLength: return "length";
    case BleFsdRejectReason::InvalidMagic: return "magic";
    case BleFsdRejectReason::InvalidVersion: return "version";
    case BleFsdRejectReason::InvalidCommand: return "command";
    case BleFsdRejectReason::InvalidState: return "state";
    case BleFsdRejectReason::InvalidCrc: return "crc";
    case BleFsdRejectReason::DuplicateSequence: return "duplicate_seq";
    case BleFsdRejectReason::OldSequence: return "old_seq";
    case BleFsdRejectReason::InvalidTimestamp: return "timestamp";
    case BleFsdRejectReason::CanUnhealthy: return "can_unhealthy";
    case BleFsdRejectReason::AwaitingClear: return "await_clear";
    default: return "unknown";
    }
}

void bleFsdReceiverStart(const BleFsdReceiverConfig &config)
{
    gConfig = config;
    gStatus.state = config.enabled ? BleFsdReceiverState::Idle : BleFsdReceiverState::Disabled;
    setReason(config.enabled ? "starting" : "disabled");
    // Do not reserve Bluetooth controller/host memory when BLE RX is disabled.
    // Discovery and a later enable request initialize the stack lazily.
    if (!config.enabled)
        return;
    if (ensureBleInitialized())
        startScan();
}

void bleFsdReceiverConfigure(const BleFsdReceiverConfig &config)
{
    const bool wasEnabled = gConfig.enabled;
    const bool wasDiscovering = gDiscoveryMode;
    const bool peerChanged =
        std::strcmp(gConfig.peerMac, config.peerMac) != 0;
    gConfig = config;
    gDiscoveryMode = false;
    gDiscoveryStartPending = false;
    if (peerChanged)
    {
        gStatus.peerMac[0] = '\0';
        gStatus.peerName[0] = '\0';
        gStatus.peerAddressType = 0;
        gStatus.rssi = 0;
        gStatus.connectedAtMs = 0;
        gStatus.lastDisconnectAtMs = 0;
    }
    if (!config.enabled)
    {
        gStatus.state = BleFsdReceiverState::Disabled;
        gStatus.remoteActive = false;
        gStatus.testRemainingMs = 0;
        gTestEndsAtMs = 0;
        setReason("disabled");
#ifdef ESP_PLATFORM
        if (gStatus.scanning)
            ble_gap_disc_cancel();
        if (gConnHandle != BLE_HS_CONN_HANDLE_NONE)
            ble_gap_terminate(gConnHandle, BLE_ERR_REM_USER_CONN_TERM);
#endif
        return;
    }
    if (!wasEnabled)
    {
        gStatus.state = BleFsdReceiverState::Idle;
        gHaveSequence = false;
        gHaveSourceTimestamp = false;
    }
#ifdef ESP_PLATFORM
    if (wasDiscovering && gStatus.scanning)
        ble_gap_disc_cancel();
    if (peerChanged && gConnHandle != BLE_HS_CONN_HANDLE_NONE)
    {
        setReason("switching_peer");
        ble_gap_terminate(gConnHandle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
#endif
    if (ensureBleInitialized())
        startScan();
}

void bleFsdReceiverTick(bool canHealthy)
{
    gCanHealthy = canHealthy;
    if (!gConfig.enabled)
    {
#ifdef ESP_PLATFORM
        if (gDiscoveryMode && static_cast<int32_t>(nowMs() - gDiscoveryEndsAtMs) >= 0)
        {
            gDiscoveryMode = false;
            if (gStatus.scanning)
                ble_gap_disc_cancel();
            setReason("discovery_done");
        }
#endif
        return;
    }
    const uint32_t now = nowMs();
    if (gDiscoveryMode &&
        static_cast<int32_t>(now - gDiscoveryEndsAtMs) >= 0 &&
        !gStatus.scanning)
    {
        gDiscoveryMode = false;
        gDiscoveryStartPending = false;
        setReason("discovery_done");
    }
    if (gStatus.state == BleFsdReceiverState::TestActive)
    {
        if (!canHealthy)
        {
            endTest("can_unhealthy", true);
            reject(BleFsdRejectReason::CanUnhealthy);
        }
        else if (static_cast<int32_t>(now - gTestEndsAtMs) >= 0)
        {
            endTest("test_complete", gStatus.remoteActive);
        }
        else
        {
            gStatus.testRemainingMs = gTestEndsAtMs - now;
        }
    }
}

BleFsdReceiverStatus bleFsdReceiverGetStatus()
{
    const uint32_t now = nowMs();
    BleFsdReceiverStatus snapshot = gStatus;
    snapshot.discoveryActive = gDiscoveryMode;
    if (snapshot.state == BleFsdReceiverState::TestActive &&
        static_cast<int32_t>(gTestEndsAtMs - now) > 0)
        snapshot.testRemainingMs = gTestEndsAtMs - now;
    return snapshot;
}

bool bleFsdReceiverStartDiscovery(uint32_t durationMs)
{
    durationMs = std::clamp(durationMs, 1000UL, 30000UL);
    gDiscoveryMode = true;
    gDiscoveryEndsAtMs = nowMs() + durationMs;
    gDiscoveryStartPending = false;
    gScanResultCount = 0;
    std::memset(gScanResults, 0, sizeof(gScanResults));
    seedConfiguredPeerScanEntry();
    setReason("discovery");
    if (!ensureBleInitialized())
    {
        gDiscoveryMode = false;
        return false;
    }
#ifdef ESP_PLATFORM
    if (gStatus.scanning)
    {
        gDiscoveryStartPending = true;
        if (ble_gap_disc_cancel() != 0)
        {
            gDiscoveryStartPending = false;
            setReason("scan_cancel_failed");
            return false;
        }
        return true;
    }
    startScan();
#endif
    return true;
}

size_t bleFsdReceiverGetScanResults(BleFsdScanEntry *out, size_t capacity)
{
    if (!out || capacity == 0)
        return gScanResultCount;
    const size_t count = std::min(capacity, gScanResultCount);
    std::memcpy(out, gScanResults, count * sizeof(BleFsdScanEntry));
    return count;
}
