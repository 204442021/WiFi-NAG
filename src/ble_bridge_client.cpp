#include "ble/bridge_client.h"
#if defined(ESP_PLATFORM) && defined(BLE_BRIDGE)
#include <algorithm>
#include <cstring>
#include "ble/brake_state.h"
#include "ble/bridge_protocol.h"
#include "obstacle_can_snapshot.h"
#include "platform/espidf_runtime.h"
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <host/ble_store.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <os/os_mbuf.h>
extern "C" void ble_store_config_init(void);
namespace
{
constexpr char kTag[] = "ble_bridge";
constexpr char kPrefs[] = "bleBridge";
constexpr uint32_t kPairMs = 120000;
constexpr uint32_t kFreshMs = 300;
constexpr uint32_t kAliveMs = 1000;
constexpr uint32_t kHelloMs = 1000;
constexpr uint32_t kHandshakeMs = 5000;
constexpr uint32_t kStateMs = 1000;
constexpr uint32_t kScanMs = 5000;
constexpr uint32_t kBackoff[] = {500, 1000, 2000, 5000};
constexpr uint8_t kRemoteRole = 0x02;
constexpr uint8_t kCommandDepth = 4;
const ble_uuid128_t kService = {{BLE_UUID_TYPE_128},
{0x01,0x00,0x00,0x00,0x00,0x00,0xd8,0x91,0x8d,0x4a,0x8f,0x7b,0x01,0xf1,0x30,0x7a}};
const ble_uuid128_t kRx = {{BLE_UUID_TYPE_128},
{0x01,0x00,0x00,0x00,0x00,0x00,0xd8,0x91,0x8d,0x4a,0x8f,0x7b,0x02,0xf1,0x30,0x7a}};
const ble_uuid128_t kTx = {{BLE_UUID_TYPE_128},
{0x01,0x00,0x00,0x00,0x00,0x00,0xd8,0x91,0x8d,0x4a,0x8f,0x7b,0x03,0xf1,0x30,0x7a}};
const ble_uuid16_t kCccd = {{BLE_UUID_TYPE_16}, BLE_GATT_DSC_CLT_CFG_UUID16};
struct Runtime
{
Shared<bool> enabled{false}, obstacle{true}, started{false}, synced{false};
Shared<bool> scanning{false}, connecting{false}, connected{false}, bonded{false};
Shared<bool> subscribed{false}, ready{false}, pairing{false}, stopping{false};
Shared<uint8_t> status{BLE_BRIDGE_PROTOCOL_DISABLED}, backoff{0};
Shared<uint16_t> conn{BLE_HS_CONN_HANDLE_NONE}, svcStart{0}, svcEnd{0};
Shared<uint16_t> rxHandle{0}, txHandle{0}, cccd{0}, lastDisconnect{0};
Shared<uint32_t> deviceId{0}, bootId{0}, peerId{0}, peerBoot{0}, pairUntil{0};
Shared<uint32_t> nextScan{0}, handshakeAt{0}, lastHello{0}, lastPacket{0};
Shared<uint32_t> lastSend{0}, lastObstacle{0}, lastState{0}, lastStateGen{0}, lastRssi{0};
Shared<uint32_t> txSeq{1}, lastRxSeq{0};
Shared<bool> haveRxSeq{false}, unbind{false}, forceState{false};
Shared<uint8_t> peerCaps{0};
Shared<int8_t> rssi{-127};
Shared<uint32_t> reconnects{0}, disconnects{0}, obstacleTx{0}, obstacleFail{0}, stateTx{0};
Shared<uint32_t> crcFail{0}, badLength{0}, badMagic{0}, badVersion{0}, unknown{0};
Shared<uint32_t> peerReject{0}, seqGap{0}, duplicateOrOld{0}, badBrakeState{0};
QueueHandle_t commands = nullptr;
} g;
uint32_t randomNonZero()
{
const uint32_t v = esp_random();
return v ? v : 1;
}
uint32_t readU32(Preferences &p, const char *key)
{
const String value = p.getString(key, "0");
return static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 10));
}
void writeU32(Preferences &p, const char *key, uint32_t value)
{
p.putString(key, String(static_cast<unsigned long>(value)));
}
void persistBool(const char *key, bool value)
{
Preferences p;
if (p.begin(kPrefs, false))
{
p.putBool(key, value);
p.end();
}
}
void persistPeer(uint32_t value)
{
Preferences p;
if (p.begin(kPrefs, false))
{
writeU32(p, "peer_id", value);
p.end();
}
}
void setStatus(BleBridgeProtocolStatus status)
{
g.status = static_cast<uint8_t>(status);
}
void resetLink()
{
brakeStateMailbox.endSession();
g.scanning = false;
g.connecting = false;
g.connected = false;
g.bonded = false;
g.subscribed = false;
g.ready = false;
g.conn = BLE_HS_CONN_HANDLE_NONE;
g.svcStart = g.svcEnd = g.rxHandle = g.txHandle = g.cccd = 0;
g.peerBoot = 0;
g.peerCaps = 0;
g.rssi = -127;
g.haveRxSeq = false;
}
void scheduleReconnect()
{
const uint8_t max = sizeof(kBackoff) / sizeof(kBackoff[0]) - 1;
const uint8_t index = std::min<uint8_t>(static_cast<uint8_t>(g.backoff), max);
g.nextScan = millis() + kBackoff[index];
if (index < max)
g.backoff = index + 1;
}
bool scanAllowed()
{
return static_cast<bool>(g.enabled) &&
(static_cast<uint32_t>(g.peerId) != 0 || static_cast<bool>(g.pairing));
}
bool advHasService(const ble_gap_disc_desc &disc)
{
if (disc.event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
disc.event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)
return false;
ble_hs_adv_fields fields{};
if (ble_hs_adv_parse_fields(&fields, disc.data, disc.length_data) != 0)
return false;
for (uint8_t i = 0; i < fields.num_uuids128; ++i)
if (ble_uuid_cmp(&fields.uuids128[i].u, &kService.u) == 0)
return true;
return false;
}
void terminate()
{
const uint16_t handle = static_cast<uint16_t>(g.conn);
if (handle != BLE_HS_CONN_HANDLE_NONE)
ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
}
int gapEvent(ble_gap_event *event, void *);
int serviceCb(uint16_t, const ble_gatt_error *, const ble_gatt_svc *, void *);
int chrCb(uint16_t, const ble_gatt_error *, const ble_gatt_chr *, void *);
int dscCb(uint16_t, const ble_gatt_error *, uint16_t, const ble_gatt_dsc *, void *);
int cccdCb(uint16_t, const ble_gatt_error *, ble_gatt_attr *, void *);
int startDiscovery()
{
g.svcStart = g.svcEnd = g.rxHandle = g.txHandle = g.cccd = 0;
setStatus(BLE_BRIDGE_PROTOCOL_DISCOVERING);
return ble_gattc_disc_svc_by_uuid(static_cast<uint16_t>(g.conn), &kService.u, serviceCb, nullptr);
}
int serviceCb(uint16_t conn, const ble_gatt_error *error, const ble_gatt_svc *svc, void *)
{
if (conn != static_cast<uint16_t>(g.conn))
return 0;
if (error->status == 0 && svc)
{
g.svcStart = svc->start_handle;
g.svcEnd = svc->end_handle;
return 0;
}
if (error->status != BLE_HS_EDONE || !static_cast<uint16_t>(g.svcStart))
{
setStatus(BLE_BRIDGE_PROTOCOL_INCOMPATIBLE);
terminate();
return 0;
}
if (ble_gattc_disc_all_chrs(conn, static_cast<uint16_t>(g.svcStart),
static_cast<uint16_t>(g.svcEnd), chrCb, nullptr) != 0)
terminate();
return 0;
}
int chrCb(uint16_t conn, const ble_gatt_error *error, const ble_gatt_chr *chr, void *)
{
if (conn != static_cast<uint16_t>(g.conn))
return 0;
if (error->status == 0 && chr)
{
if (ble_uuid_cmp(&chr->uuid.u, &kRx.u) == 0)
g.rxHandle = chr->val_handle;
else if (ble_uuid_cmp(&chr->uuid.u, &kTx.u) == 0)
g.txHandle = chr->val_handle;
return 0;
}
if (error->status != BLE_HS_EDONE || !static_cast<uint16_t>(g.rxHandle) ||
!static_cast<uint16_t>(g.txHandle))
{
setStatus(BLE_BRIDGE_PROTOCOL_INCOMPATIBLE);
terminate();
return 0;
}
if (ble_gattc_disc_all_dscs(conn, static_cast<uint16_t>(g.txHandle),
static_cast<uint16_t>(g.svcEnd), dscCb, nullptr) != 0)
terminate();
return 0;
}
int dscCb(uint16_t conn, const ble_gatt_error *error, uint16_t, const ble_gatt_dsc *dsc, void *)
{
if (conn != static_cast<uint16_t>(g.conn))
return 0;
if (error->status == 0 && dsc)
{
if (ble_uuid_cmp(&dsc->uuid.u, &kCccd.u) == 0)
g.cccd = dsc->handle;
return 0;
}
if (error->status != BLE_HS_EDONE || !static_cast<uint16_t>(g.cccd))
{
setStatus(BLE_BRIDGE_PROTOCOL_INCOMPATIBLE);
terminate();
return 0;
}
const uint8_t value[2] = {1, 0};
if (ble_gattc_write_flat(conn, static_cast<uint16_t>(g.cccd), value, sizeof(value), cccdCb, nullptr) != 0)
terminate();
return 0;
}
int cccdCb(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *, void *)
{
if (conn != static_cast<uint16_t>(g.conn))
return 0;
if (error->status != 0)
{
terminate();
return 0;
}
g.subscribed = true;
g.handshakeAt = millis();
g.lastHello = 0;
setStatus(BLE_BRIDGE_PROTOCOL_HANDSHAKE);
return 0;
}
uint32_t nextSequence()
{
const uint32_t value = static_cast<uint32_t>(g.txSeq);
g.txSeq = value + 1;
return value;
}
bool sendPacket(BleBridgeProtocol::Packet &packet, bool obstacle, bool state)
{
if (!static_cast<bool>(g.connected) || !static_cast<uint16_t>(g.rxHandle))
return false;
BleBridgeProtocol::finalize(packet);
const int rc = ble_gattc_write_no_rsp_flat(static_cast<uint16_t>(g.conn),
static_cast<uint16_t>(g.rxHandle),
packet.bytes,
BleBridgeProtocol::kPacketSize);
if (rc == 0)
{
g.lastSend = millis();
if (obstacle)
g.obstacleTx = static_cast<uint32_t>(g.obstacleTx) + 1;
if (state)
g.stateTx = static_cast<uint32_t>(g.stateTx) + 1;
return true;
}
if (obstacle)
g.obstacleFail = static_cast<uint32_t>(g.obstacleFail) + 1;
return false;
}
bool sendHello()
{
auto packet = BleBridgeProtocol::makePacket(BleBridgeProtocol::MSG_HELLO, 0, nextSequence());
uint8_t *body = BleBridgeProtocol::payload(packet);
BleBridgeProtocol::writeLe32(body, static_cast<uint32_t>(g.deviceId));
BleBridgeProtocol::writeLe32(body + 4, static_cast<uint32_t>(g.bootId));
body[8] = BleBridgeProtocol::kAdvertisedCapabilities;
body[9] = 0x01;
return sendPacket(packet, false, false);
}
bool sendNag(bool periodic)
{
const NagStateView state = nagStateController.view();
auto packet = BleBridgeProtocol::makePacket(BleBridgeProtocol::MSG_NAG_STATE, 0, nextSequence());
uint8_t *body = BleBridgeProtocol::payload(packet);
body[0] = state.configuredEnabled;
body[1] = state.runtimeEffective;
body[2] = periodic ? NAG_RESULT_OK : state.result;
body[3] = periodic ? NAG_SOURCE_PERIODIC_SYNC : state.source;
BleBridgeProtocol::writeLe16(body + 4, periodic ? 0 : state.ackCommandId);
BleBridgeProtocol::writeLe32(body + 6, state.revision);
if (!sendPacket(packet, false, true))
return false;
g.lastState = millis();
g.lastStateGen = state.reportGeneration;
g.forceState = false;
return true;
}
bool sendObstacle()
{
ObstacleCanView snapshot;
if (!obstacleCanSnapshot.read(snapshot))
return false;
const uint32_t now = millis();
const uint32_t age255 = snapshot.has255 ? now - snapshot.last255RxMs : UINT32_MAX;
const uint32_t age12b = snapshot.has12B ? now - snapshot.last12BRxMs : UINT32_MAX;
const bool fresh255 = snapshot.has255 && snapshot.dlc255Valid && age255 <= kFreshMs;
const bool fresh12b = snapshot.has12B && snapshot.dlc12BValid && age12b <= kFreshMs;
const bool alive = (snapshot.has255 && age255 <= kAliveMs) || (snapshot.has12B && age12b <= kAliveMs);
const NagStateView nag = nagStateController.view();
auto packet = BleBridgeProtocol::makePacket(BleBridgeProtocol::MSG_OBSTACLE_STATE, 0, nextSequence());
uint8_t *body = BleBridgeProtocol::payload(packet);
std::memcpy(body, snapshot.raw255, 4);
std::memcpy(body + 4, snapshot.raw12B, 4);
body[8] = (fresh255 ? 1u : 0u) | (fresh12b ? 2u : 0u) | (alive ? 4u : 0u) |
(snapshot.dlc255Valid ? 8u : 0u) | (snapshot.dlc12BValid ? 16u : 0u) |
(nag.configuredEnabled ? 32u : 0u) | (nag.runtimeEffective ? 64u : 0u) | 128u;
const uint8_t gear = snapshot.dlc255Valid ? (snapshot.raw255[3] & 0x03u) : 0;
const uint8_t direction = snapshot.dlc12BValid ? ((snapshot.raw12B[1] >> 4) & 0x07u) : 0;
body[9] = gear | (direction << 2);
if (!sendPacket(packet, true, false))
return false;
g.lastObstacle = now;
return true;
}
void acceptHello(const uint8_t *data)
{
const uint8_t *body = data + BleBridgeProtocol::kPayloadOffset;
const uint32_t peer = BleBridgeProtocol::readLe32(body);
const uint32_t saved = static_cast<uint32_t>(g.peerId);
if (!peer || body[9] != kRemoteRole ||
    !BleBridgeProtocol::supportsRequiredCapabilities(body[8]) ||
    (saved && saved != peer))
{
g.peerReject = static_cast<uint32_t>(g.peerReject) + 1;
setStatus(BLE_BRIDGE_PROTOCOL_INCOMPATIBLE);
terminate();
return;
}
if (!saved)
{
const uint32_t now = millis();
if (!static_cast<bool>(g.pairing) || static_cast<int32_t>(now - static_cast<uint32_t>(g.pairUntil)) >= 0)
{
g.peerReject = static_cast<uint32_t>(g.peerReject) + 1;
terminate();
return;
}
g.peerId = peer;
persistPeer(peer);
g.pairing = false;
}
g.peerBoot = BleBridgeProtocol::readLe32(body + 4);
g.peerCaps = body[8];
brakeStateMailbox.beginSession(
    static_cast<uint32_t>(g.peerBoot),
    BleBridgeProtocol::supportsBrakeState(body[8]),
    millis());
g.ready = true;
g.backoff = 0;
setStatus(BLE_BRIDGE_PROTOCOL_READY);
nagStateController.forceReport();
g.forceState = true;
g.lastObstacle = 0;
}
void handlePacket(const uint8_t *data)
{
const uint32_t sequence = BleBridgeProtocol::readLe32(data + 4);
const bool helloSessionBoundary =
    BleBridgeProtocol::isHelloSessionBoundary(data[2]);
if (helloSessionBoundary)
{
const uint8_t *helloBody = data + BleBridgeProtocol::kPayloadOffset;
const uint32_t incomingBootId = BleBridgeProtocol::readLe32(helloBody + 4);
const BleBridgeProtocol::HelloAckDisposition helloDisposition =
    BleBridgeProtocol::classifyHelloAck(
        incomingBootId,
        static_cast<uint32_t>(g.peerBoot),
        static_cast<bool>(g.ready),
        sequence,
        static_cast<uint32_t>(g.lastRxSeq),
        static_cast<bool>(g.haveRxSeq));
if (helloDisposition == BleBridgeProtocol::HELLO_DUPLICATE_OR_OLD)
{
g.duplicateOrOld = static_cast<uint32_t>(g.duplicateOrOld) + 1U;
return;
}
const bool replacingReadySession = static_cast<bool>(g.ready);
g.ready = false;
if (replacingReadySession)
brakeStateMailbox.endSession();
g.haveRxSeq = true;
g.lastRxSeq = sequence;
g.lastPacket = millis();
acceptHello(data);
return;
}
const BleBridgeProtocol::SequenceDisposition disposition =
    BleBridgeProtocol::classifySequence(
        sequence,
        static_cast<uint32_t>(g.lastRxSeq),
        static_cast<bool>(g.haveRxSeq));
if (disposition == BleBridgeProtocol::SEQUENCE_DUPLICATE_OR_OLD)
{
g.duplicateOrOld = static_cast<uint32_t>(g.duplicateOrOld) + 1U;
return;
}
if (disposition == BleBridgeProtocol::SEQUENCE_GAP)
g.seqGap = static_cast<uint32_t>(g.seqGap) + 1U;
g.haveRxSeq = true;
g.lastRxSeq = sequence;
g.lastPacket = millis();
if (!static_cast<bool>(g.ready))
return;
if (data[2] == BleBridgeProtocol::MSG_QUERY_NAG)
{
nagStateController.forceReport();
g.forceState = true;
return;
}
if (data[2] == BleBridgeProtocol::MSG_BRAKE_STATE)
{
if (!brakeStateMailbox.publish(data + BleBridgeProtocol::kPayloadOffset,
                               sequence,
                               millis()))
g.badBrakeState = static_cast<uint32_t>(g.badBrakeState) + 1U;
return;
}
if (data[2] != BleBridgeProtocol::MSG_SET_NAG)
{
g.unknown = static_cast<uint32_t>(g.unknown) + 1;
return;
}
const uint8_t *body = data + BleBridgeProtocol::kPayloadOffset;
NagRemoteCommand command;
command.desiredEnabled = body[0] != 0;
command.persist = body[1] != 0;
command.source = body[2];
command.commandId = BleBridgeProtocol::readLe16(body + 4);
command.expectedRevision = BleBridgeProtocol::readLe32(body + 6);
if (body[0] > 1 || body[1] != 1 || body[2] != 1 || body[3] != 0)
{
nagStateController.reportRemoteRejection(command, NAG_RESULT_BAD_COMMAND);
g.forceState = true;
return;
}
if (!g.commands || xQueueSend(g.commands, &command, 0) != pdTRUE)
{
nagStateController.reportRemoteRejection(command, NAG_RESULT_BUSY);
g.forceState = true;
}
}
void handleNotify(const os_mbuf *om)
{
const uint16_t length = OS_MBUF_PKTLEN(om);
if (length != BleBridgeProtocol::kPacketSize)
{
g.badLength = static_cast<uint32_t>(g.badLength) + 1;
return;
}
uint8_t data[BleBridgeProtocol::kPacketSize]{};
if (os_mbuf_copydata(om, 0, sizeof(data), data) != 0)
return;
const auto error = BleBridgeProtocol::validate(data, sizeof(data));
if (error != BleBridgeProtocol::DECODE_OK)
{
if (error == BleBridgeProtocol::DECODE_BAD_MAGIC)
g.badMagic = static_cast<uint32_t>(g.badMagic) + 1;
else if (error == BleBridgeProtocol::DECODE_BAD_VERSION)
g.badVersion = static_cast<uint32_t>(g.badVersion) + 1;
else if (error == BleBridgeProtocol::DECODE_BAD_CRC)
g.crcFail = static_cast<uint32_t>(g.crcFail) + 1;
return;
}
if (!BleBridgeProtocol::isKnownMessageType(data[2]))
{
g.unknown = static_cast<uint32_t>(g.unknown) + 1;
return;
}
handlePacket(data);
}
int gapEvent(ble_gap_event *event, void *)
{
switch (event->type)
{
case BLE_GAP_EVENT_DISC:
if (!scanAllowed() || static_cast<bool>(g.connecting) || !advHasService(event->disc))
return 0;
ble_gap_disc_cancel();
g.scanning = false;
g.connecting = true;
g.reconnects = static_cast<uint32_t>(g.reconnects) + 1;
setStatus(BLE_BRIDGE_PROTOCOL_CONNECTING);
{
uint8_t own = 0;
int rc = ble_hs_id_infer_auto(0, &own);
if (rc == 0)
rc = ble_gap_connect(own, &event->disc.addr, 10000, nullptr, gapEvent, nullptr);
if (rc != 0)
{
g.connecting = false;
scheduleReconnect();
}
}
return 0;
case BLE_GAP_EVENT_CONNECT:
g.scanning = g.connecting = false;
if (event->connect.status != 0)
{
resetLink();
scheduleReconnect();
return 0;
}
g.connected = true;
g.conn = event->connect.conn_handle;
setStatus(BLE_BRIDGE_PROTOCOL_DISCOVERING);
{
const int rc = ble_gap_security_initiate(event->connect.conn_handle);
if (rc == BLE_HS_EALREADY)
{
if (startDiscovery() != 0)
terminate();
}
else if (rc != 0)
terminate();
}
return 0;
case BLE_GAP_EVENT_ENC_CHANGE:
if (event->enc_change.status != 0)
{
terminate();
return 0;
}
{
ble_gap_conn_desc desc{};
if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0)
g.bonded = desc.sec_state.bonded != 0;
}
if (startDiscovery() != 0)
terminate();
return 0;
case BLE_GAP_EVENT_NOTIFY_RX:
if (event->notify_rx.attr_handle == static_cast<uint16_t>(g.txHandle) && event->notify_rx.om)
handleNotify(event->notify_rx.om);
return 0;
case BLE_GAP_EVENT_DISCONNECT:
g.lastDisconnect = event->disconnect.reason;
g.disconnects = static_cast<uint32_t>(g.disconnects) + 1;
resetLink();
setStatus(static_cast<bool>(g.enabled) ? BLE_BRIDGE_PROTOCOL_IDLE : BLE_BRIDGE_PROTOCOL_DISABLED);
scheduleReconnect();
return 0;
case BLE_GAP_EVENT_DISC_COMPLETE:
g.scanning = false;
if (!static_cast<bool>(g.connected) && !static_cast<bool>(g.connecting))
scheduleReconnect();
return 0;
case BLE_GAP_EVENT_REPEAT_PAIRING:
if (!static_cast<bool>(g.pairing))
return BLE_GAP_REPEAT_PAIRING_IGNORE;
{
ble_gap_conn_desc desc{};
if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0)
ble_store_util_delete_peer(&desc.peer_id_addr);
}
return BLE_GAP_REPEAT_PAIRING_RETRY;
default:
return 0;
}
}
void startScan()
{
if (!scanAllowed() || !static_cast<bool>(g.synced) || static_cast<bool>(g.scanning) ||
static_cast<bool>(g.connecting) || static_cast<bool>(g.connected))
return;
uint8_t own = 0;
if (ble_hs_id_infer_auto(0, &own) != 0)
return;
ble_gap_disc_params params{};
params.filter_duplicates = 1;
params.passive = 0;
params.itvl = 0x80;
params.window = 0x20;
if (ble_gap_disc(own, kScanMs, &params, gapEvent, nullptr) == 0)
{
g.scanning = true;
setStatus(BLE_BRIDGE_PROTOCOL_SCANNING);
}
else
scheduleReconnect();
}
void hostReset(int reason)
{
ESP_LOGE(kTag, "NimBLE reset: %d", reason);
g.synced = false;
resetLink();
setStatus(BLE_BRIDGE_PROTOCOL_ERROR);
}
void hostSync()
{
if (ble_hs_util_ensure_addr(0) == 0)
{
g.synced = true;
g.nextScan = millis();
if (static_cast<bool>(g.enabled))
setStatus(BLE_BRIDGE_PROTOCOL_IDLE);
}
}
void hostTask(void *)
{
nimble_port_run();
nimble_port_freertos_deinit();
}
void bridgeTask(void *)
{
g.started = true;
for (;;)
{
const uint32_t now = millis();
nagStateController.pollRuntime();
NagRemoteCommand command;
while (g.commands && xQueueReceive(g.commands, &command, 0) == pdTRUE)
{
nagStateController.applyRemote(command);
g.forceState = true;
}
if (static_cast<bool>(g.unbind))
{
if (static_cast<bool>(g.scanning))
ble_gap_disc_cancel();
terminate();
ble_store_clear();
g.peerId = g.peerBoot = 0;
g.pairing = false;
persistPeer(0);
resetLink();
g.unbind = false;
}
if (static_cast<bool>(g.stopping) || !static_cast<bool>(g.enabled))
{
if (static_cast<bool>(g.scanning))
ble_gap_disc_cancel();
terminate();
setStatus(BLE_BRIDGE_PROTOCOL_DISABLED);
vTaskDelay(pdMS_TO_TICKS(50));
continue;
}
if (static_cast<bool>(g.pairing) && static_cast<int32_t>(now - static_cast<uint32_t>(g.pairUntil)) >= 0)
g.pairing = false;
if (!static_cast<bool>(g.connected))
{
if (scanAllowed() && static_cast<int32_t>(now - static_cast<uint32_t>(g.nextScan)) >= 0)
startScan();
vTaskDelay(pdMS_TO_TICKS(20));
continue;
}
if (now - static_cast<uint32_t>(g.lastRssi) >= 2000)
{
int8_t value = -127;
if (ble_gap_conn_rssi(static_cast<uint16_t>(g.conn), &value) == 0)
g.rssi = value;
g.lastRssi = now;
}
if (static_cast<bool>(g.subscribed) && !static_cast<bool>(g.ready))
{
if (now - static_cast<uint32_t>(g.handshakeAt) > kHandshakeMs)
terminate();
else if (!static_cast<uint32_t>(g.lastHello) || now - static_cast<uint32_t>(g.lastHello) >= kHelloMs)
{
if (sendHello())
g.lastHello = now;
}
vTaskDelay(pdMS_TO_TICKS(20));
continue;
}
if (!static_cast<bool>(g.ready))
{
vTaskDelay(pdMS_TO_TICKS(20));
continue;
}
const NagStateView state = nagStateController.view();
if (static_cast<bool>(g.forceState) || state.reportGeneration != static_cast<uint32_t>(g.lastStateGen) ||
now - static_cast<uint32_t>(g.lastState) >= kStateMs)
{
sendNag(state.reportGeneration == static_cast<uint32_t>(g.lastStateGen));
}
if (static_cast<bool>(g.obstacle) &&
BleBridgeTiming::obstacleStateDue(millis(), static_cast<uint32_t>(g.lastObstacle)))
sendObstacle();
const uint32_t serviceDelayMs = BleBridgeTiming::nextObstacleServiceDelayMs(
millis(), static_cast<uint32_t>(g.lastObstacle), static_cast<bool>(g.obstacle));
vTaskDelay(pdMS_TO_TICKS(serviceDelayMs));
}
}
}
BleBridgeClient bleBridgeClient;
void BleBridgeClient::begin()
{
if (static_cast<bool>(g.started))
return;
Preferences p;
if (p.begin(kPrefs, false))
{
g.enabled = p.getBool("enabled", false);
g.obstacle = p.getBool("obs_fwd", true);
obstacleShiftFeatureEnabled = false;
p.putBool("shift_dr", false);
uint32_t id = readU32(p, "dev_id");
if (!id)
{
id = randomNonZero();
writeU32(p, "dev_id", id);
}
g.deviceId = id;
g.peerId = readU32(p, "peer_id");
p.end();
}
else
g.deviceId = randomNonZero();
g.bootId = randomNonZero();
g.txSeq = randomNonZero();
g.commands = xQueueCreate(kCommandDepth, sizeof(NagRemoteCommand));
if (!g.commands || nimble_port_init() != ESP_OK)
{
setStatus(BLE_BRIDGE_PROTOCOL_ERROR);
return;
}
ble_hs_cfg.reset_cb = hostReset;
ble_hs_cfg.sync_cb = hostSync;
ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
ble_hs_cfg.sm_bonding = 1;
ble_hs_cfg.sm_mitm = 0;
ble_hs_cfg.sm_sc = 1;
ble_hs_cfg.sm_sc_only = 1;
ble_hs_cfg.sm_sec_lvl = 2;
ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
ble_store_config_init();
nimble_port_freertos_init(hostTask);
TaskHandle_t task = nullptr;
#if CONFIG_FREERTOS_UNICORE
const BaseType_t ok = xTaskCreate(bridgeTask, "ble_bridge", 6144, nullptr, 3, &task);
#else
const BaseType_t ok = xTaskCreatePinnedToCore(bridgeTask, "ble_bridge", 6144, nullptr, 3, &task, 1);
#endif
if (ok != pdPASS)
setStatus(BLE_BRIDGE_PROTOCOL_ERROR);
}
void BleBridgeClient::setEnabled(bool value, bool persist)
{
g.enabled = value;
if (persist)
persistBool("enabled", value);
if (value)
{
g.stopping = false;
g.nextScan = millis();
setStatus(BLE_BRIDGE_PROTOCOL_IDLE);
}
else
{
g.pairing = false;
g.ready = false;
brakeStateMailbox.endSession();
setStatus(BLE_BRIDGE_PROTOCOL_DISABLED);
}
}
bool BleBridgeClient::enabled() const { return static_cast<bool>(g.enabled); }
void BleBridgeClient::setObstacleForwarding(bool value, bool persist)
{
g.obstacle = value;
if (value)
g.lastObstacle = 0;
if (persist)
persistBool("obs_fwd", value);
}
bool BleBridgeClient::obstacleForwarding() const { return static_cast<bool>(g.obstacle); }
void BleBridgeClient::setObstacleShiftEnabled(bool value, bool persist)
{
(void)value;
obstacleShiftFeatureEnabled = false;
if (persist)
persistBool("shift_dr", false);
}
bool BleBridgeClient::obstacleShiftEnabled() const
{
return static_cast<bool>(obstacleShiftFeatureEnabled);
}
bool BleBridgeClient::startPairing()
{
if (static_cast<uint32_t>(g.peerId))
return false;
setEnabled(true, true);
g.pairing = true;
g.pairUntil = millis() + kPairMs;
g.nextScan = millis();
return true;
}
void BleBridgeClient::unbind() { g.unbind = true; }
void BleBridgeClient::prepareForRestart()
{
g.stopping = true;
g.enabled = false;
g.pairing = false;
g.ready = false;
brakeStateMailbox.endSession();
setStatus(BLE_BRIDGE_PROTOCOL_DISABLED);
}
void BleBridgeClient::forceNagState() { g.forceState = true; }
BleBridgeDiagnostics BleBridgeClient::diagnostics() const
{
BleBridgeDiagnostics out;
const uint32_t now = millis();
out.enabled = static_cast<bool>(g.enabled);
out.obstacleForwarding = static_cast<bool>(g.obstacle);
out.taskStarted = static_cast<bool>(g.started);
out.scanning = static_cast<bool>(g.scanning);
out.connecting = static_cast<bool>(g.connecting);
out.connected = static_cast<bool>(g.connected);
out.bonded = static_cast<bool>(g.bonded);
out.subscribed = static_cast<bool>(g.subscribed);
out.bridgeReady = static_cast<bool>(g.ready);
out.pairing = static_cast<bool>(g.pairing);
if (out.pairing)
{
const uint32_t until = static_cast<uint32_t>(g.pairUntil);
out.pairingRemainingMs = static_cast<int32_t>(until - now) > 0 ? until - now : 0;
}
out.deviceId = static_cast<uint32_t>(g.deviceId);
out.bootId = static_cast<uint32_t>(g.bootId);
out.peerDeviceId = static_cast<uint32_t>(g.peerId);
out.peerBootId = static_cast<uint32_t>(g.peerBoot);
out.peerCapabilities = static_cast<uint8_t>(g.peerCaps);
out.rssi = static_cast<int8_t>(g.rssi);
out.protocolStatus = static_cast<BleBridgeProtocolStatus>(static_cast<uint8_t>(g.status));
out.lastDisconnectReason = static_cast<uint16_t>(g.lastDisconnect);
const uint32_t packet = static_cast<uint32_t>(g.lastPacket);
out.hasLastPacket = packet != 0;
out.lastPacketAgeMs = packet ? now - packet : 0;
const uint32_t sent = static_cast<uint32_t>(g.lastSend);
out.hasLastSend = sent != 0;
out.lastSendAgeMs = sent ? now - sent : 0;
ObstacleCanView snapshot;
obstacleCanSnapshot.read(snapshot);
out.dlc255Valid = snapshot.dlc255Valid;
out.dlc12BValid = snapshot.dlc12BValid;
out.last255AgeMs = snapshot.has255 ? now - snapshot.last255RxMs : UINT32_MAX;
out.last12BAgeMs = snapshot.has12B ? now - snapshot.last12BRxMs : UINT32_MAX;
out.fresh255 = snapshot.has255 && snapshot.dlc255Valid && out.last255AgeMs <= kFreshMs;
out.fresh12B = snapshot.has12B && snapshot.dlc12BValid && out.last12BAgeMs <= kFreshMs;
out.partyCanAlive = (snapshot.has255 && out.last255AgeMs <= kAliveMs) ||
(snapshot.has12B && out.last12BAgeMs <= kAliveMs);
out.suggestedGear = snapshot.dlc255Valid ? snapshot.raw255[3] & 0x03u : 0;
out.torqueDirection = snapshot.dlc12BValid ? (snapshot.raw12B[1] >> 4) & 0x07u : 0;
const NagStateView nag = nagStateController.view();
out.hasLastRemoteCommand = nag.hasLastCommand;
out.lastRemoteDesired = nag.lastCommandDesired;
out.lastRemoteCommandId = nag.lastCommandId;
out.lastCommandResult = nag.lastCommandResult;
out.reconnectCount = static_cast<uint32_t>(g.reconnects);
out.disconnectCount = static_cast<uint32_t>(g.disconnects);
out.obstacleTxCount = static_cast<uint32_t>(g.obstacleTx);
out.obstacleTxFailCount = static_cast<uint32_t>(g.obstacleFail);
out.stateReportCount = static_cast<uint32_t>(g.stateTx);
out.crcFailCount = static_cast<uint32_t>(g.crcFail);
out.badLengthCount = static_cast<uint32_t>(g.badLength);
out.badMagicCount = static_cast<uint32_t>(g.badMagic);
out.badVersionCount = static_cast<uint32_t>(g.badVersion);
out.unknownTypeCount = static_cast<uint32_t>(g.unknown);
out.peerRejectCount = static_cast<uint32_t>(g.peerReject);
out.sequenceGapCount = static_cast<uint32_t>(g.seqGap);
out.duplicateOrOldSequenceCount = static_cast<uint32_t>(g.duplicateOrOld);
out.badBrakeStateCount = static_cast<uint32_t>(g.badBrakeState);
return out;
}
#endif
