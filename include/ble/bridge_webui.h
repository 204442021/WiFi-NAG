#pragma once

#if defined(ESP_PLATFORM) && defined(BLE_BRIDGE)

#include <climits>

#include "ble/bridge_client.h"
#include "nag_state_controller.h"
#include "obstacle_can_snapshot.h"

static const char *bleBridgeProtocolName(BleBridgeProtocolStatus status)
{
    switch (status)
    {
    case BLE_BRIDGE_PROTOCOL_DISABLED: return "已关闭";
    case BLE_BRIDGE_PROTOCOL_IDLE: return "等待连接";
    case BLE_BRIDGE_PROTOCOL_SCANNING: return "扫描中";
    case BLE_BRIDGE_PROTOCOL_CONNECTING: return "连接中";
    case BLE_BRIDGE_PROTOCOL_DISCOVERING: return "服务发现";
    case BLE_BRIDGE_PROTOCOL_HANDSHAKE: return "握手中";
    case BLE_BRIDGE_PROTOCOL_READY: return "正常";
    case BLE_BRIDGE_PROTOCOL_INCOMPATIBLE: return "协议不兠容";
    default: return "错误";
    }
}

static const char *bleBridgeResultName(NagResultCode result)
{
    switch (result)
    {
    case NAG_RESULT_OK: return "成功";
    case NAG_RESULT_BAD_COMMAND: return "命令错误";
    case NAG_RESULT_REVISION_CONFLICT: return "Revision 冲突";
    case NAG_RESULT_BUSY_OTA: return "OTA 忙";
    case NAG_RESULT_CAN_UNAVAILABLE: return "CAN 不可用";
    case NAG_RESULT_UNSUPPORTED: return "不支持";
    case NAG_RESULT_BUSY: return "队列忙";
    default: return "内部错误";
    }
}

static NagResultCode bleBridgeApplyNagState(bool desiredEnabled,
                                             bool persist,
                                             NagChangeSource source)
{
    if (source == NAG_SOURCE_FSD_REMOTE && Update.isRunning())
        return NAG_RESULT_BUSY_OTA;
    if (!appDriver || appCanRestartPreparing)
        return NAG_RESULT_CAN_UNAVAILABLE;

    if (persist)
    {
        Preferences statePreferences;
        if (!statePreferences.begin(PREFS_NS, false))
            return NAG_RESULT_INTERNAL_ERROR;
        statePreferences.putBool("can", desiredEnabled);
        statePreferences.putBool("nag_en", true);
        statePreferences.end();
    }

    const bool changed = canActive != desiredEnabled;
    canActive = desiredEnabled;
    dashApplyRuntimeState();
    if (changed && source == NAG_SOURCE_LOCAL_WEBUI)
        dashLog(String("[BLE] authoritative NAG ") +
                (desiredEnabled ? "ON via local WebUI" : "OFF via local WebUI"));
    return NAG_RESULT_OK;
}

static bool bleBridgeNagRuntimeEffective()
{
    return canActive && nagKillerEnabled && static_cast<bool>(nagKillerRuntime) &&
           appCanWriteModeKnown && appLastWriteEnabled && canOnline &&
           !Update.isRunning() && !appCanRestartPreparing;
}

static void bleBridgeFrameObserver(const CanFrame &frame)
{
    mcpDashOnFrame(frame);
    if (frame.id == 0x255 || frame.id == 0x12B)
        obstacleCanSnapshot.observe(frame, millis());
}

static void bleBridgeAppendAge(String &json, const char *name,
                               bool available, uint32_t ageMs)
{
    json += ",\"";
    json += name;
    json += "\":";
    if (available)
        json += String(ageMs);
    else
        json += "null";
}

static void bleBridgeHandleStatus()
{
    const BleBridgeDiagnostics diagnostics = bleBridgeClient.diagnostics();
    const NagStateView nag = nagStateController.view();
    String json = "{\"ok\":true";
    json.reserve(1900);
#define BLE_JSON_BOOL(name, value) do { json += ",\"" name "\":"; json += ((value) ? "true" : "false"); } while (0)
    BLE_JSON_BOOL("enabled", diagnostics.enabled);
    BLE_JSON_BOOL("obstacleForwarding", diagnostics.obstacleForwarding);
    BLE_JSON_BOOL("taskStarted", diagnostics.taskStarted);
    BLE_JSON_BOOL("scanning", diagnostics.scanning);
    BLE_JSON_BOOL("connecting", diagnostics.connecting);
    BLE_JSON_BOOL("connected", diagnostics.connected);
    BLE_JSON_BOOL("bonded", diagnostics.bonded);
    BLE_JSON_BOOL("subscribed", diagnostics.subscribed);
    BLE_JSON_BOOL("bridgeReady", diagnostics.bridgeReady);
    BLE_JSON_BOOL("pairing", diagnostics.pairing);
    BLE_JSON_BOOL("fresh255", diagnostics.fresh255);
    BLE_JSON_BOOL("fresh12B", diagnostics.fresh12B);
    BLE_JSON_BOOL("partyCanAlive", diagnostics.partyCanAlive);
    BLE_JSON_BOOL("dlc255Valid", diagnostics.dlc255Valid);
    BLE_JSON_BOOL("dlc12BValid", diagnostics.dlc12BValid);
    BLE_JSON_BOOL("hasLastRemoteCommand", diagnostics.hasLastRemoteCommand);
    BLE_JSON_BOOL("lastRemoteDesired", diagnostics.lastRemoteDesired);
    BLE_JSON_BOOL("nagConfigured", nag.configuredEnabled);
    BLE_JSON_BOOL("nagRuntime", nag.runtimeEffective);
#undef BLE_JSON_BOOL
    json += ",\"pairingRemainingMs\":" + String(diagnostics.pairingRemainingMs);
    json += ",\"deviceId\":" + String(diagnostics.deviceId);
    json += ",\"bootId\":" + String(diagnostics.bootId);
    json += ",\"peerDeviceId\":" + String(diagnostics.peerDeviceId);
    json += ",\"peerBootId\":" + String(diagnostics.peerBootId);
    json += ",\"peerCapabilities\":" + String(diagnostics.peerCapabilities);
    json += ",\"rssi\":" + String(static_cast<int>(diagnostics.rssi));
    json += ",\"protocol\":" + String(static_cast<unsigned>(diagnostics.protocolStatus));
    json += ",\"protocolName\":\"" + String(bleBridgeProtocolName(diagnostics.protocolStatus)) + "\"";
    json += ",\"lastDisconnectReason\":" + String(diagnostics.lastDisconnectReason);
    bleBridgeAppendAge(json, "lastPacketAgeMs", diagnostics.hasLastPacket, diagnostics.lastPacketAgeMs);
    bleBridgeAppendAge(json, "lastSendAgeMs", diagnostics.hasLastSend, diagnostics.lastSendAgeMs);
    bleBridgeAppendAge(json, "last255AgeMs", diagnostics.last255AgeMs != UINT32_MAX, diagnostics.last255AgeMs);
    bleBridgeAppendAge(json, "last12BAgeMs", diagnostics.last12BAgeMs != UINT32_MAX, diagnostics.last12BAgeMs);
    json += ",\"suggestedGear\":" + String(diagnostics.suggestedGear);
    json += ",\"torqueDirection\":" + String(diagnostics.torqueDirection);
    json += ",\"nagRevision\":" + String(nag.revision);
    json += ",\"lastRemoteCommandId\":" + String(diagnostics.lastRemoteCommandId);
    json += ",\"lastCommandResult\":" + String(static_cast<unsigned>(diagnostics.lastCommandResult));
    json += ",\"lastCommandResultName\":\"" + String(bleBridgeResultName(diagnostics.lastCommandResult)) + "\"";
    json += ",\"reconnectCount\":" + String(diagnostics.reconnectCount);
    json += ",\"disconnectCount\":" + String(diagnostics.disconnectCount);
    json += ",\"obstacleTxCount\":" + String(diagnostics.obstacleTxCount);
    json += ",\"obstacleTxFailCount\":" + String(diagnostics.obstacleTxFailCount);
    json += ",\"stateReportCount\":" + String(diagnostics.stateReportCount);
    json += ",\"crcFailCount\":" + String(diagnostics.crcFailCount);
    json += ",\"badLengthCount\":" + String(diagnostics.badLengthCount);
    json += ",\"badMagicCount\":" + String(diagnostics.badMagicCount);
    json += ",\"badVersionCount\":" + String(diagnostics.badVersionCount);
    json += ",\"unknownTypeCount\":" + String(diagnostics.unknownTypeCount);
    json += ",\"peerRejectCount\":" + String(diagnostics.peerRejectCount);
    json += ",\"sequenceGapCount\":" + String(diagnostics.sequenceGapCount);
    json += ",\"setCommandCount\":" + String(nag.setCommandCount);
    json += ",\"duplicateCommandCount\":" + String(nag.duplicateCommandCount);
    json += ",\"revisionConflictCount\":" + String(nag.revisionConflictCount);
    json += ",\"commandRejectCount\":" + String(nag.commandRejectCount);
    json += "}";
    server.send(200, "application/json", json);
}

static bool bleBridgeArgEnabled(const char *name, bool fallback)
{
    if (!server.hasArg(name))
        return fallback;
    const String value = server.arg(name);
    return value == "1" || value == "true" || value == "on";
}

static void bleBridgeHandleConfig()
{
    bleBridgeClient.setEnabled(bleBridgeArgEnabled("enabled", bleBridgeClient.enabled()), true);
    bleBridgeClient.setObstacleForwarding(
        bleBridgeArgEnabled("obstacle", bleBridgeClient.obstacleForwarding()), true);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void bleBridgeHandlePair()
{
    if (!bleBridgeClient.startPairing())
    {
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"请先解除现有绑定\"}");
        return;
    }
    dashLog("[BLE] 120-second pairing window started");
    server.send(200, "application/json", "{\"ok\":true}");
}

static void bleBridgeHandleUnbind()
{
    bleBridgeClient.unbind();
    dashLog("[BLE] peer unbind requested");
    server.send(200, "application/json", "{\"ok\":true}");
}

static void bleBridgeHandleUnifiedConfig()
{
    bool ok = true;
    if (server.hasArg("can") || server.hasArg("force"))
    {
        const bool desired = server.hasArg("can") ? server.arg("can") == "1"
                                                   : server.arg("force") == "1";
        const NagStateView state = nagStateController.applyLocal(
            desired, NAG_SOURCE_LOCAL_WEBUI, true);
        ok = state.result == NAG_RESULT_OK;
    }
#if defined(NAG_KILLER)
    nagKillerEnabled = true;
    const bool nagConfigChanged = dashApplyNagConfigArgs();
    dashApplyRuntimeState();
    if (nagConfigChanged)
        dashSavePrefs();
#endif
    bleBridgeClient.forceNagState();
    server.send(ok ? 200 : 500, "application/json",
                ok ? "{\"ok\":true,\"product\":\"wifi-nag\"}"
                   : "{\"ok\":false,\"error\":\"NAG state apply failed\"}");
}

static void bleBridgeHandleDisable()
{
    const NagStateView state = nagStateController.applyLocal(
        false, NAG_SOURCE_LOCAL_WEBUI, true);
    bleBridgeClient.forceNagState();
    server.send(state.result == NAG_RESULT_OK ? 200 : 500,
                "text/plain",
                state.result == NAG_RESULT_OK ? "Injection stopped."
                                              : "NAG state apply failed.");
}

static void bleBridgeRegisterApiRoutes()
{
    server.on("/ble_status", HTTP_GET, bleBridgeHandleStatus);
    server.on("/ble_config", HTTP_POST, bleBridgeHandleConfig);
    server.on("/ble_pair", HTTP_POST, bleBridgeHandlePair);
    server.on("/ble_unbind", HTTP_POST, bleBridgeHandleUnbind);
    server.on("/config", HTTP_POST, bleBridgeHandleUnifiedConfig);
    server.on("/disable", HTTP_POST, bleBridgeHandleDisable);
}

static void bleBridgeAfterDashboardSetup()
{
    nagStateController.configureCallbacks(bleBridgeApplyNagState,
                                          bleBridgeNagRuntimeEffective);
    nagStateController.begin(canActive);

    if (dashHandler)
        dashHandler->onFrame = bleBridgeFrameObserver;
    if (dashDriver)
    {
        static constexpr uint32_t observedIds[] = {0x370, 0x255, 0x12B};
        dashDriver->setFilters(observedIds, 3);
    }

    bleBridgeClient.begin();
    dashLog("[BOOT] BLE bridge ready: 0x255/0x12B observer + authoritative NAG sync");
}

#endif // ESP_PLATFORM && BLE_BRIDGE
