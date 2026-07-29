#!/usr/bin/env python3
"""One-shot, assertion-driven migration for the V1.0.1 firmware info feature."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION = ROOT / "VERSION"
CMAKE = ROOT / "CMakeLists.txt"
DASH = ROOT / "include" / "web" / "mcp2515_dashboard.h"
UI = ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def update_version_and_cmake() -> None:
    VERSION.write_text("V1.0.1\n", encoding="utf-8")
    CMAKE.write_text(
        """cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" PROJECT_VER LIMIT_COUNT 1)
string(STRIP "${PROJECT_VER}" PROJECT_VER)
if(PROJECT_VER STREQUAL "")
    message(FATAL_ERROR "VERSION must contain the firmware version")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ev_open_can_tools)
""",
        encoding="utf-8",
    )


def update_dashboard_backend() -> None:
    text = DASH.read_text(encoding="utf-8")

    text = replace_once(
        text,
        "#include <driver/temperature_sensor.h>\n#include <esp_chip_info.h>",
        "#include <driver/temperature_sensor.h>\n#include <esp_app_desc.h>\n#include <esp_chip_info.h>",
        "include esp_app_desc",
    )
    text = replace_once(
        text,
        "#include <esp_ota_ops.h>\n#include <esp_pm.h>",
        "#include <esp_ota_ops.h>\n#include <esp_partition.h>\n#include <esp_pm.h>",
        "include esp_partition",
    )
    text = replace_once(
        text,
        "#if !defined(PRODUCT_WIFI_NAG)\n#error \"This firmware is maintained as WIFI-NAG only.\"\n#endif\n",
        "#if !defined(PRODUCT_WIFI_NAG)\n#error \"This firmware is maintained as WIFI-NAG only.\"\n#endif\n\n#ifndef FIRMWARE_VERSION\n#define FIRMWARE_VERSION \"unknown\"\n#endif\n",
        "early firmware version fallback",
    )
    text = replace_once(
        text,
        "static bool canOnline = false;\n\nstatic unsigned long fpsFrames = 0;",
        "static bool canOnline = false;\nstatic String lastOtaUploadTime = \"\";\n\nstatic unsigned long fpsFrames = 0;",
        "OTA time state",
    )

    helper_code = r'''static bool dashValidOtaTime(const String &value)
{
    if (value.length() != 19)
        return false;
    for (unsigned int i = 0; i < value.length(); i++)
    {
        const char c = value.charAt(i);
        const bool separator = i == 4 || i == 7 || i == 10 || i == 13 || i == 16;
        if (separator)
        {
            const char expected = i == 4 || i == 7 ? '-' : (i == 10 ? ' ' : ':');
            if (c != expected)
                return false;
        }
        else if (c < '0' || c > '9')
        {
            return false;
        }
    }
    return true;
}

static bool dashPersistOtaTime(const String &otaTime)
{
    if (!dashValidOtaTime(otaTime))
        return false;
    if (!prefs.begin(PREFS_NS, false))
        return false;
    prefs.putString("ota_time", otaTime);
    prefs.end();
    lastOtaUploadTime = otaTime;
    return true;
}

#ifdef ESP_PLATFORM
static const char *dashFirmwareVersion()
{
    const esp_app_desc_t *description = esp_app_get_description();
    if (description && description->version[0] != '\0')
        return description->version;
    return FIRMWARE_VERSION;
}

static const char *dashOtaPartitionName(const esp_partition_t *running)
{
    if (!running)
        return "unknown";
    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0)
        return "OTA_0";
    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1)
        return "OTA_1";
    return running->label[0] ? running->label : "unknown";
}
#endif

'''
    text = replace_once(
        text,
        "    return out;\n}\n\nstatic bool dashInjectionActive()",
        "    return out;\n}\n\n" + helper_code + "static bool dashInjectionActive()",
        "firmware metadata helpers",
    )

    text = replace_once(
        text,
        "    prefs.begin(PREFS_NS, false);\n    uint8_t storedHw = prefs.getUChar(\"hw\", DASH_DEFAULT_HW);",
        "    prefs.begin(PREFS_NS, false);\n    lastOtaUploadTime = prefs.getString(\"ota_time\", \"\");\n    if (!lastOtaUploadTime.isEmpty() && !dashValidOtaTime(lastOtaUploadTime))\n    {\n        prefs.remove(\"ota_time\");\n        lastOtaUploadTime = \"\";\n    }\n    uint8_t storedHw = prefs.getUChar(\"hw\", DASH_DEFAULT_HW);",
        "load OTA time from NVS",
    )

    text = replace_once(
        text,
        "        if (upload.totalSize > 0 && Update.end(true) && Update.isFinished())\n            dashLog(\"[OTA] Done: \" + String(upload.totalSize) + \" bytes\");\n        else",
        "        if (upload.totalSize > 0 && Update.end(true) && Update.isFinished())\n        {\n            dashPersistOtaTime(server.arg(\"ota_time\"));\n            dashLog(\"[OTA] Done: \" + String(upload.totalSize) + \" bytes\");\n        }\n        else",
        "persist successful OTA time",
    )

    text = replace_once(
        text,
        '    j += ",\\"firmware\\":\\"" FIRMWARE_VERSION "\\"";\n',
        '    j += ",\\"firmware\\":\\"" + jsonEscape(String(dashFirmwareVersion())) + "\\"";\n'
        '    j += ",\\"ota_partition\\":\\"" + String(dashOtaPartitionName(running)) + "\\"";\n'
        '    j += ",\\"ota_time\\":\\"" + jsonEscape(lastOtaUploadTime) + "\\"";\n',
        "system status firmware metadata",
    )

    text = replace_once(
        text,
        "\n#ifndef FIRMWARE_VERSION\n#define FIRMWARE_VERSION \"unknown\"\n#endif\n\n// Dashboard frame callback wrapper",
        "\n// Dashboard frame callback wrapper",
        "remove late firmware version fallback",
    )

    DASH.write_text(text, encoding="utf-8")


def update_dashboard_ui() -> None:
    text = UI.read_text(encoding="utf-8-sig")

    old_card = '''<div class="card" id="firmware-update-card">
  <div class="card-hdr">
    <div class="card-title">Firmware Update <span class="title-help" aria-label="Help" onclick="return toggleHelp(this,event)" title="Manual firmware upload only. Select a local .bin and flash it to the device.">i</span></div>
    <div class="card-meta" id="fw-ver">Manual OTA</div>
  </div>
  <div style="margin-top:4px">'''
    new_card = '''<div class="card" id="firmware-update-card">
  <div class="card-hdr">
    <div class="card-title">Firmware Update <span class="title-help" aria-label="Help" onclick="return toggleHelp(this,event)" title="Manual firmware upload only. Select a local .bin and flash it to the device.">i</span></div>
    <div class="card-meta" id="fw-ver">Manual OTA</div>
  </div>
  <div class="sys-grid" style="margin:4px 0 12px">
    <div class="sys-item"><div class="sys-lbl">Firmware Version</div><div class="sys-val" id="fw-version">--</div></div>
    <div class="sys-item"><div class="sys-lbl">Current Partition</div><div class="sys-val" id="fw-partition">--</div></div>
    <div class="sys-item sys-wide"><div class="sys-lbl">OTA Upload Time</div><div class="sys-val" id="fw-ota-time">--</div></div>
  </div>
  <div style="margin-top:4px">'''
    text = replace_once(text, old_card, new_card, "firmware update metadata card")

    text = replace_once(
        text,
        "  'Firmware Update':'固件更新','Manual OTA':'手动 OTA',",
        "  'Firmware Update':'固件更新','Manual OTA':'手动 OTA','Firmware Version':'固件版本','Current Partition':'当前分区','OTA Upload Time':'OTA 上传时间','Not recorded':'未记录',",
        "firmware metadata translations",
    )

    firmware_functions = '''async function loadFirmwareInfo(){
  try{
    const d=await fetchPollJson('/system_status',2500);
    setText('fw-version',d.firmware||'unknown');
    setText('fw-partition',d.ota_partition||d.app_label||'unknown');
    setText('fw-ota-time',d.ota_time||'Not recorded');
  }catch(e){
    setText('fw-version','unknown');
    setText('fw-partition','unknown');
    setText('fw-ota-time','Not recorded');
  }
}
function formatOtaLocalTime(date){
  const pad=value=>String(value).padStart(2,'0');
  return date.getFullYear()+'-'+pad(date.getMonth()+1)+'-'+pad(date.getDate())+' '+pad(date.getHours())+':'+pad(date.getMinutes())+':'+pad(date.getSeconds());
}
'''
    text = replace_once(
        text,
        "// OTA upload\nfunction fileSelected(file){",
        firmware_functions + "// OTA upload\nfunction fileSelected(file){",
        "firmware info loader",
    )

    text = replace_once(
        text,
        "  xhr.open('POST','/update',true,otaUser,otaPass);",
        "  const otaTime=formatOtaLocalTime(new Date());\n  xhr.open('POST','/update?ota_time='+encodeURIComponent(otaTime),true,otaUser,otaPass);",
        "OTA browser time query",
    )

    visibility_block = """document.addEventListener('visibilitychange',()=>{
  if(!dashboardVisible())return;
  poll();loadWifiStatus();loadApStatus();loadGatewayStatus();"""
    text = replace_once(
        text,
        visibility_block,
        """document.addEventListener('visibilitychange',()=>{
  if(!dashboardVisible())return;
  poll();loadFirmwareInfo();loadWifiStatus();loadApStatus();loadGatewayStatus();""",
        "refresh firmware info on visibility",
    )

    text = replace_once(
        text,
        "orderDashboardCards();initCardMinimizers();initSubsectionMinimizers();if(isCarUiActive())expandCarEssentials();initSystemMonitor();loadGatewayDnsCached();",
        "orderDashboardCards();initCardMinimizers();initSubsectionMinimizers();if(isCarUiActive())expandCarEssentials();initSystemMonitor();loadFirmwareInfo();loadGatewayDnsCached();",
        "initial firmware info load",
    )

    UI.write_text(text, encoding="utf-8-sig")


def main() -> None:
    update_version_and_cmake()
    update_dashboard_backend()
    update_dashboard_ui()
    print("Applied WiFi-NAG V1.0.1 firmware metadata changes")


if __name__ == "__main__":
    main()
