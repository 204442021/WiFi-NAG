#pragma once

#if defined(ESP_PLATFORM) && defined(BLE_BRIDGE)

#include <climits>

#include "ble/bridge_client.h"
#include "nag_state_controller.h"
#include "obstacle_can_snapshot.h"

static const char BLE_BRIDGE_SHELL[] PROGMEM = R"BLESH(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WIFI-NAG</title></head><body><div id="loading" style="font-family:sans-serif;padding:24px">正在加载 WIFI-NAG…</div>
<script>
(async()=>{
  try{
    const response=await fetch('/dashboard',{cache:'no-store'});
    if(!response.ok)throw new Error('HTTP '+response.status);
    let html=await response.text();
    const hotspotTag=/<div\b[^>]*\bid\s*=\s*(?:"wifi-hotspot-section"|'wifi-hotspot-section'|wifi-hotspot-section)(?=[\s>])[^>]*>/i;
    const card=`
  <div class="subsec" id="ble-bridge-section" data-subkey="config-ble-bridge">
    <div class="subsec-head">
      <div class="subsec-title">BLE 联动 <span class="title-help" title="与 T2CAN-FSD 一对一绑定，转发 0x255/0x12B，并同步 NAG 权威状态。">i</span></div>
      <div class="subsec-meta" id="ble-card-meta">未连接</div>
    </div>
    <div class="subsec-body">
      <div class="info-box">BLE 为独立低优先级旁路。关闭或断线不会改变本地 NAG，也不会阻塞 0x370 快速路径。</div>
      <div class="setting-row">
        <div class="setting-info"><div class="setting-name">BLE 联动总开关</div><div class="setting-desc">关闭后停止扫描、连接和状态同步，本地 NAG 保持原状态。</div></div>
        <label class="tgl"><input type="checkbox" id="ble-enabled" onchange="bleSaveConfig()"><div class="tgl-track"><div class="tgl-thumb"></div></div></label>
      </div>
      <div class="setting-row">
        <div class="setting-info"><div class="setting-name">障碍物数据转发</div><div class="setting-desc">每 100 ms 发送最新 0x255 / 0x12B；不补发历史帧。</div></div>
        <label class="tgl"><input type="checkbox" id="ble-obstacle" onchange="bleSaveConfig()"><div class="tgl-track"><div class="tgl-thumb"></div></div></label>
      </div>
      <div class="btn-row">
        <button class="sniff-btn" id="ble-pair-btn" onclick="bleStartPairing()">开始配对（120 秒）</button>
        <button class="sniff-btn" id="ble-unbind-btn" onclick="bleUnbind()">解除绑定</button>
        <button class="sniff-btn" onclick="bleLoadStatus()">刷新</button>
      </div>
      <div id="ble-action-msg" class="setting-desc" style="margin-top:8px"></div>
      <div class="sys-grid" style="margin-top:12px">
        <div class="sys-item"><div class="sys-lbl">FSD 设备</div><div class="sys-val" id="ble-device-state">--</div></div>
        <div class="sys-item"><div class="sys-lbl">协议状态</div><div class="sys-val" id="ble-protocol">--</div></div>
        <div class="sys-item"><div class="sys-lbl">本机 / 对端 ID</div><div class="sys-val" id="ble-peer-id">--</div></div>
        <div class="sys-item"><div class="sys-lbl">RSSI / 最后通信</div><div class="sys-val" id="ble-radio">--</div></div>
        <div class="sys-item"><div class="sys-lbl">NAG 配置</div><div class="sys-val" id="ble-nag-config">--</div></div>
        <div class="sys-item"><div class="sys-lbl">NAG 运行</div><div class="sys-val" id="ble-nag-runtime">--</div></div>
        <div class="sys-item"><div class="sys-lbl">FSD 同步</div><div class="sys-val" id="ble-nag-sync">--</div></div>
        <div class="sys-item"><div class="sys-lbl">Revision / 命令</div><div class="sys-val" id="ble-nag-revision">--</div></div>
        <div class="sys-item"><div class="sys-lbl">0x255</div><div class="sys-val" id="ble-255">--</div></div>
        <div class="sys-item"><div class="sys-lbl">0x12B</div><div class="sys-val" id="ble-12b">--</div></div>
        <div class="sys-item"><div class="sys-lbl">当前方向摘要</div><div class="sys-val" id="ble-summary">--</div></div>
        <div class="sys-item"><div class="sys-lbl">FSD 接收 / 最后发送</div><div class="sys-val" id="ble-fsd-rx">--</div></div>
        <div class="sys-item sys-wide"><div class="sys-lbl">诊断计数</div><div class="sys-val" id="ble-counters">--</div></div>
      </div>
    </div>
  </div>
  <script src="/ble_ui.js"><\/script>
`;
    const hotspot=hotspotTag.exec(html);
    if(hotspot){
      html=html.slice(0,hotspot.index)+card+html.slice(hotspot.index);
    }else{
      console.warn('BLE card insertion point missing; showing dashboard without BLE card');
    }
    document.open();document.write(html);document.close();
  }catch(error){document.getElementById('loading').textContent='页面加载失败：'+error.message;}
})();
</script></body></html>)BLESH";

static const char BLE_BRIDGE_UI_JS[] PROGMEM = R"BLEJS((()=>{
const b=id=>document.getElementById(id);
const text=(id,value)=>{const el=b(id);if(el)el.textContent=value;};
const age=value=>value===null||value===undefined?'--':(value+' ms');
const shortId=value=>value?('0x'+Number(value).toString(16).toUpperCase().padStart(8,'0')):'未绑定';
const gearName=value=>({0:'无',1:'D',2:'R'}[Number(value)]||('未知('+value+')'));
const dirName=value=>({2:'R',3:'D'}[Number(value)]||('无/未知('+value+')'));
function bleOpenCard(){const sec=b('ble-bridge-section');if(!sec)return;sec.classList.remove('collapsed');if(sec.dataset.collapseKey)localStorage.setItem(sec.dataset.collapseKey,'0');const btn=sec.querySelector('.subsec-btn');if(btn)btn.textContent=(window.trText?trText('Hide'):'收起');}
function bleSetMessage(message,ok){const el=b('ble-action-msg');if(!el)return;el.textContent=message||'';el.style.color=ok?'var(--ok)':'var(--err)';}
window.bleLoadStatus=async function(){
  try{
    const r=await fetch('/ble_status',{cache:'no-store'});const d=await r.json();if(!r.ok||d.ok===false)throw new Error(d.error||('HTTP '+r.status));
    if(b('ble-enabled'))b('ble-enabled').checked=!!d.enabled;
    if(b('ble-obstacle'))b('ble-obstacle').checked=!!d.obstacleForwarding;
    const device=d.pairing?'配对中 '+Math.ceil((d.pairingRemainingMs||0)/1000)+'s':(d.bridgeReady?'已连接':(d.connected?'握手中':(d.connecting?'连接中':(d.scanning?'扫描中':'离线'))));
    text('ble-card-meta',device);text('ble-device-state',device+(d.bonded?' · 已绑定':'')+(d.lastDisconnectReason?' · 原因 '+d.lastDisconnectReason:''));
    text('ble-protocol',d.protocolName+' · '+(d.subscribed?'Notify 已订阅':'Notify 未订阅'));
    text('ble-peer-id',shortId(d.deviceId)+' / '+shortId(d.peerDeviceId));
    text('ble-radio',(d.rssi>-127?d.rssi+' dBm':'--')+' / '+age(d.lastPacketAgeMs));
    text('ble-nag-config',d.nagConfigured?'已开启':'已关闭');
    text('ble-nag-runtime',d.nagRuntime?'有效':'暂不可用');
    text('ble-nag-sync',d.bridgeReady?'已同步':(d.connected?'等待同步':'离线'));
    const cmd=d.hasLastRemoteCommand?('#'+d.lastRemoteCommandId+' '+(d.lastRemoteDesired?'远程开启':'远程关闭')):'无';
    text('ble-nag-revision',d.nagRevision+' / '+cmd+' / '+d.lastCommandResultName);
    text('ble-255',(d.fresh255?'新鲜':'过期')+' · '+age(d.last255AgeMs)+' · DLC '+(d.dlc255Valid?'4':'异常'));
    text('ble-12b',(d.fresh12B?'新鲜':'过期')+' · '+age(d.last12BAgeMs)+' · DLC '+(d.dlc12BValid?'4':'异常'));
    text('ble-summary','建议 '+gearName(d.suggestedGear)+' / 方向 '+dirName(d.torqueDirection)+' / Party CAN '+(d.partyCanAlive?'在线':'过期'));
    text('ble-fsd-rx',(d.bridgeReady?'正常':'未确认')+' / '+age(d.lastSendAgeMs));
    text('ble-counters','obstacle '+d.obstacleTxCount+'/'+d.obstacleTxFailCount+' · state '+d.stateReportCount+' · reconnect '+d.reconnectCount+' · disconnect '+d.disconnectCount+' · CRC '+d.crcFailCount+' · bad '+(d.badLengthCount+d.badMagicCount+d.badVersionCount+d.unknownTypeCount)+' · conflict '+d.revisionConflictCount+' · duplicate '+d.duplicateCommandCount);
    if(b('ble-pair-btn'))b('ble-pair-btn').disabled=!!d.peerDeviceId||!!d.pairing;
    if(b('ble-unbind-btn'))b('ble-unbind-btn').disabled=!d.peerDeviceId;
  }catch(error){text('ble-card-meta','状态不可用');bleSetMessage(error.message,false);}
};
window.bleSaveConfig=async function(){
  try{const body='enabled='+(b('ble-enabled').checked?'1':'0')+'&obstacle='+(b('ble-obstacle').checked?'1':'0');const r=await fetch('/ble_config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();if(!r.ok||!d.ok)throw new Error(d.error||'保存失败');bleSetMessage('BLE 配置已保存',true);bleLoadStatus();}catch(error){bleSetMessage(error.message,false);}
};
window.bleStartPairing=async function(){
  try{const r=await fetch('/ble_pair',{method:'POST'});const d=await r.json();if(!r.ok||!d.ok)throw new Error(d.error||'无法开始配对');bleSetMessage('已开启 120 秒配对窗口',true);bleLoadStatus();}catch(error){bleSetMessage(error.message,false);}
};
window.bleUnbind=async function(){
  if(!confirm('确认解除 FSD 一对一绑定？解除后需重新配对。'))return;
  try{const r=await fetch('/ble_unbind',{method:'POST'});const d=await r.json();if(!r.ok||!d.ok)throw new Error(d.error||'解除失败');bleSetMessage('已提交解除绑定请求',true);setTimeout(bleLoadStatus,300);}catch(error){bleSetMessage(error.message,false);}
};
window.addEventListener('DOMContentLoaded',()=>{bleOpenCard();bleLoadStatus();setInterval(()=>{if(!document.hidden)bleLoadStatus();},2000);});
})();)BLEJS";

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

static void bleBridgeHandleShell()
{
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.send(200, "text/html; charset=utf-8", BLE_BRIDGE_SHELL);
}

static void bleBridgeHandleOriginalDashboard()
{
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendRaw(200, "text/html",
                   reinterpret_cast<const char *>(DASH_HTML_GZ),
                   DASH_HTML_GZ_LEN);
}

static void bleBridgeHandleUiScript()
{
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "application/javascript; charset=utf-8", BLE_BRIDGE_UI_JS);
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

static void bleBridgeRegisterDashboardRoutes()
{
    // Registered before mcpDashboardSetup(); WebServer resolves the first exact
    // route, so these wrappers preserve the original dashboard while adding a
    // separate BLE card and a single authoritative NAG state entry point.
    server.on("/", HTTP_GET, bleBridgeHandleShell);
    server.on("/dashboard", HTTP_GET, bleBridgeHandleOriginalDashboard);
    server.on("/ble_ui.js", HTTP_GET, bleBridgeHandleUiScript);
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
