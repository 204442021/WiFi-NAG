#pragma once

#if defined(ESP_PLATFORM) && defined(BLE_BRIDGE)

#include <climits>

#include "ble/bridge_client.h"
#include "ble/brake_state.h"
#include "nag_state_controller.h"
#include "obstacle_can_snapshot.h"
#include "obstacle_shift_controller.h"

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

static const char *bleBridgeShiftStateName(ObstacleShiftState state)
{
    switch (state)
    {
    case OBSTACLE_SHIFT_WAIT_RELEASE: return "等待释放";
    case OBSTACLE_SHIFT_ARMED: return "已就绪";
    case OBSTACLE_SHIFT_ACTIVE_P: return "虚拟 P 发送中";
    case OBSTACLE_SHIFT_LATCHED: return "已锁存";
    default: return "未知";
    }
}

static const char *bleBridgeShiftReasonName(ObstacleShiftReason reason)
{
    switch (reason)
    {
    case SHIFT_REASON_NONE: return "无";
    case SHIFT_REASON_WAIT_RELEASE: return "等待释放";
    case SHIFT_REASON_ACTIVE: return "正在发送";
    case SHIFT_REASON_WINDOW_COMPLETE: return "500 ms 窗口完成";
    case SHIFT_REASON_BRAKE_STALE: return "刹车数据超时";
    case SHIFT_REASON_RELEASE_UNCONFIRMED: return "释放未确认";
    case SHIFT_REASON_SESSION_CHANGED: return "BLE 会话变化";
    case SHIFT_REASON_LINK_UNAVAILABLE: return "BLE 链路不可用";
    case SHIFT_REASON_CAPABILITY_MISSING: return "对端不支持刹车状态";
    case SHIFT_REASON_FEATURE_DISABLED: return "功能已关闭";
    case SHIFT_REASON_CAN_WRITE_DISABLED: return "CAN 写入不可用";
    case SHIFT_REASON_118_STALE: return "0x118 数据超时";
    case SHIFT_REASON_GEAR_NOT_DR: return "真实挡位非 D/R";
    case SHIFT_REASON_MOVING: return "车辆未确认静止";
    case SHIFT_REASON_TX_FAILED: return "虚拟 P 发送失败";
    default: return "未知";
    }
}

static const char *bleBridgeGearName(BrakeRealGear gear)
{
    switch (gear)
    {
    case BRAKE_GEAR_P: return "P";
    case BRAKE_GEAR_R: return "R";
    case BRAKE_GEAR_N: return "N";
    case BRAKE_GEAR_D: return "D";
    default: return "未知";
    }
}

static const char *bleBridgeBrakeReasonName(BrakeStateReason reason)
{
    switch (reason)
    {
    case BRAKE_REASON_IDLE: return "空闲";
    case BRAKE_REASON_ACTIVE: return "制动有效";
    case BRAKE_REASON_RELEASE_CONFIRMED: return "释放已确认";
    case BRAKE_REASON_STALE: return "数据超时";
    case BRAKE_REASON_CONFLICT: return "双源冲突";
    case BRAKE_REASON_FALLBACK: return "降级来源";
    case BRAKE_REASON_HOLD_PENDING: return "等待保持";
    case BRAKE_REASON_GEAR_UNKNOWN: return "挡位未知";
    case BRAKE_REASON_GEAR_STALE: return "挡位超时";
    case BRAKE_REASON_GEAR_NOT_DR: return "挡位非 D/R";
    case BRAKE_REASON_SPEED_UNKNOWN: return "车速未知";
    case BRAKE_REASON_SPEED_STALE: return "车速超时";
    case BRAKE_REASON_MOVING: return "车辆移动中";
    case BRAKE_REASON_DISABLED: return "发送端已关闭";
    case BRAKE_REASON_SESSION_RESYNC: return "会话重新同步";
    default: return "未知";
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
    const uint32_t now = millis();
    const BleBridgeDiagnostics diagnostics = bleBridgeClient.diagnostics();
    const NagStateView nag = nagStateController.view();
    BrakeStateView brake;
    const bool haveBrakeView = brakeStateMailbox.read(brake);
    const ObstacleShiftView shift = obstacleShiftController.view(now);
    String json = "{\"ok\":true";
    json.reserve(3600);
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
    BLE_JSON_BOOL("shiftEnabled", bleBridgeClient.obstacleShiftEnabled());
    BLE_JSON_BOOL("brakeLinkReady", haveBrakeView && brake.linkReady);
    BLE_JSON_BOOL("brakeCapabilitySupported", haveBrakeView && brake.capabilitySupported);
    BLE_JSON_BOOL("brakeStateFresh", shift.brakeStateFresh);
    BLE_JSON_BOOL("brakePressed", haveBrakeView && brake.data.brakePressed);
    BLE_JSON_BOOL("releaseConfirmed", haveBrakeView && brake.data.releaseConfirmed);
    BLE_JSON_BOOL("physicalKnown", haveBrakeView && brake.data.physicalKnown);
    BLE_JSON_BOOL("physicalFresh", haveBrakeView && brake.data.physicalFresh);
    BLE_JSON_BOOL("physicalPressed", haveBrakeView && brake.data.physicalPressed);
    BLE_JSON_BOOL("systemKnown", haveBrakeView && brake.data.systemKnown);
    BLE_JSON_BOOL("systemFresh", haveBrakeView && brake.data.systemFresh);
    BLE_JSON_BOOL("systemPressed", haveBrakeView && brake.data.systemPressed);
    BLE_JSON_BOOL("sourcesAgree", haveBrakeView && brake.data.sourcesAgree);
    BLE_JSON_BOOL("fallbackActive", haveBrakeView && brake.data.fallbackActive);
    BLE_JSON_BOOL("gearFresh", haveBrakeView && brake.data.gearFresh);
    BLE_JSON_BOOL("drConfirmed", haveBrakeView && brake.data.drConfirmed);
    BLE_JSON_BOOL("brakeHoldReady", haveBrakeView && brake.data.brakeHoldReady);
    BLE_JSON_BOOL("activeEligible", haveBrakeView && brake.data.activeEligible);
    BLE_JSON_BOOL("speedFresh", haveBrakeView && brake.data.speedFresh);
    BLE_JSON_BOOL("stationaryConfirmed", haveBrakeView && brake.data.stationaryConfirmed);
    BLE_JSON_BOOL("releaseTail", haveBrakeView && brake.data.releaseTail);
    BLE_JSON_BOOL("senderEnabled", haveBrakeView && brake.data.senderEnabled);
    BLE_JSON_BOOL("normalRuntime", haveBrakeView && brake.data.normalRuntime);
    BLE_JSON_BOOL("hasReal118", shift.hasReal118);
    BLE_JSON_BOOL("virtualParkActive", shift.virtualParkActive);
    BLE_JSON_BOOL("shiftLatched", shift.latched);
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
    bleBridgeAppendAge(json, "brakeStateAgeMs",
                       haveBrakeView && brake.hasState,
                       haveBrakeView && brake.hasState ? now - brake.lastRxMs : 0);
    bleBridgeAppendAge(json, "real118AgeMs", shift.hasReal118,
                       shift.real118AgeMs);
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
    json += ",\"duplicateOrOldSequenceCount\":" + String(diagnostics.duplicateOrOldSequenceCount);
    json += ",\"badBrakeStateCount\":" + String(diagnostics.badBrakeStateCount);
    json += ",\"setCommandCount\":" + String(nag.setCommandCount);
    json += ",\"duplicateCommandCount\":" + String(nag.duplicateCommandCount);
    json += ",\"revisionConflictCount\":" + String(nag.revisionConflictCount);
    json += ",\"commandRejectCount\":" + String(nag.commandRejectCount);
    json += ",\"shiftState\":" + String(static_cast<unsigned>(shift.state));
    json += ",\"shiftStateName\":\"" + String(bleBridgeShiftStateName(shift.state)) + "\"";
    json += ",\"shiftReason\":" + String(static_cast<unsigned>(shift.reason));
    json += ",\"shiftReasonName\":\"" + String(bleBridgeShiftReasonName(shift.reason)) + "\"";
    json += ",\"brakeSessionGeneration\":" + String(brake.sessionGeneration);
    json += ",\"brakeSequence\":" + String(brake.sequence);
    json += ",\"brakeRealGear\":" + String(static_cast<unsigned>(brake.data.realGear));
    json += ",\"brakeRealGearName\":\"" + String(bleBridgeGearName(brake.data.realGear)) + "\"";
    json += ",\"gearSource\":" + String(brake.data.gearSource);
    json += ",\"senderReason\":" + String(static_cast<unsigned>(brake.data.reason));
    json += ",\"senderReasonName\":\"" + String(bleBridgeBrakeReasonName(brake.data.reason)) + "\"";
    json += ",\"brakeHoldMs\":" + String(brake.data.brakeHoldMs);
    json += ",\"speedDeciKph\":" + String(brake.data.speedDeciKph);
    json += ",\"real118Gear\":" + String(static_cast<unsigned>(shift.realGear));
    json += ",\"real118GearName\":\"" + String(bleBridgeGearName(shift.realGear)) + "\"";
    json += ",\"virtualParkRemainingMs\":" + String(shift.virtualParkRemainingMs);
    json += ",\"real118RxCount\":" + String(shift.real118RxCount);
    json += ",\"virtualParkTxCount\":" + String(shift.virtualParkTxCount);
    json += ",\"virtualParkTxFailCount\":" + String(shift.virtualParkTxFailCount);
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
    bleBridgeClient.setObstacleShiftEnabled(
        bleBridgeArgEnabled("shift", bleBridgeClient.obstacleShiftEnabled()),
        true);
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
        static constexpr uint32_t observedIds[] = {0x370, 0x255, 0x12B, 0x118};
        dashDriver->setFilters(observedIds,
                               static_cast<uint8_t>(sizeof(observedIds) /
                                                    sizeof(observedIds[0])));
    }

    bleBridgeClient.begin();
    dashLog("[BOOT] BLE bridge ready: 0x255/0x12B/0x118 observer + authoritative NAG sync");
}

#endif // ESP_PLATFORM && BLE_BRIDGE
