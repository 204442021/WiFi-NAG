#pragma once

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)

#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h"
#else
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Update.h>
#endif
#include <esp_task_wdt.h>
#include <cstdarg>
#ifdef ESP_PLATFORM
#include <driver/temperature_sensor.h>
#include <esp_app_desc.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_image_format.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <esp_spiffs.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <driver/gpio.h>
#include <esp_private/esp_clk.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#ifndef ESP_PLATFORM
#include <Preferences.h>
#include <SPIFFS.h>
#endif
#include "handlers.h"
#include "can_helpers.h"
#include <ArduinoJson.h>
#include "web/memory_diagnostics.h"
#include "web/mcp2515_dashboard_ui.h"

#if !defined(PRODUCT_WIFI_NAG)
#error "This firmware is maintained as WIFI-NAG only."
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
#ifndef FIRMWARE_GIT_SHA
#define FIRMWARE_GIT_SHA "unknown"
#endif

#ifndef DASH_SSID
#error "Define -DDASH_SSID in build_flags (e.g. -DDASH_SSID=\\\"WIFI-NAG-1234\\\")"
#endif
#ifndef DASH_PASS
#error "Define -DDASH_PASS in build_flags (min 8 chars)"
#endif
#ifndef DASH_OTA_PASS
#error "Define -DDASH_OTA_PASS in build_flags"
#endif
#ifndef DASH_OTA_USER
#error "Define -DDASH_OTA_USER in build_flags"
#endif

static_assert(sizeof(DASH_SSID) > 1 && sizeof(DASH_SSID) <= 33, "DASH_SSID must be 1-32 bytes");
static_assert(sizeof(DASH_PASS) >= 9 && sizeof(DASH_PASS) <= 65, "DASH_PASS must be 8-64 bytes");

#ifndef DASH_DEFAULT_HW
#define DASH_DEFAULT_HW 1
#endif

#if defined(DASH_INJECTION_ON_BOOT)
static constexpr bool kDashInjectionDefaultEnabled = true;
#else
static constexpr bool kDashInjectionDefaultEnabled = false;
#endif

#if defined(DRIVER_TWAI)
#ifndef TWAI_TX_PIN
#define TWAI_TX_PIN GPIO_NUM_5
#endif
#ifndef TWAI_RX_PIN
#define TWAI_RX_PIN GPIO_NUM_4
#endif
#endif

#if DASH_DEFAULT_HW != 1
#error "DASH_DEFAULT_HW must be 1 for WIFI-NAG"
#endif

#define PREFS_NS "ADunlock"
static constexpr uint8_t kDashUnsetU8 = 0xFF;

static Preferences prefs;

static CarManagerBase *dashHandler = nullptr;
static CanDriver *dashDriver = nullptr;

static unsigned long rxCount = 0;
static unsigned long txCount = 0;
static unsigned long txErrCount = 0;
static unsigned long lastFrameMs = 0;
static unsigned long startMs = 0;
static bool canOnline = false;
static String lastOtaUploadTime = "";

static unsigned long fpsFrames = 0;
static unsigned long fpsLastMs = 0;
static float fps = 0.0f;

static uint8_t hwMode = DASH_DEFAULT_HW;
static bool canActive = kDashInjectionDefaultEnabled;
static bool dashBootForcedNagOff = false;
static bool dashBootNagRevisionPending = false;
static bool dashOtaCanPrepared = false;
static volatile bool dashRestartPending = false;
static volatile uint32_t dashRestartAtMs = 0;
static constexpr char kDashOtaNagForceOffKey[] = "ota_nag_off";
static constexpr char kDashCanFirmwareAddressKey[] = "can_fw_addr";
static constexpr char kDashCanFirmwareVersionKey[] = "can_fw_ver";
static constexpr char kDashNagBootRevisionKey[] = "nag_boot_rev";
#if defined(NAG_KILLER)
// User-facing Nag killer switch (WebUI). Actual CAN echo TX is additionally
// gated by canActive (the global CAN/injection master switch), so nothing is
// transmitted until CAN injection is enabled.
[[maybe_unused]] static bool nagKillerEnabled = true;
#else
[[maybe_unused]] static bool nagKillerEnabled = false;
#endif

#ifdef RGB_BRIGHTNESS
static constexpr uint8_t kDashLedBrightnessDefault = RGB_BRIGHTNESS;
#else
static constexpr uint8_t kDashLedBrightnessDefault = 32;
#endif
static constexpr uint8_t dashLedBrightness = kDashLedBrightnessDefault;

// WiFi AP (hotspot) — name is fixed; password/visibility remain configurable.
static char apSSID[33] = "";
static char apPass[65] = "";
static bool apHidden = false; // when true, SSID is not broadcast (hidden AP)
static constexpr char kDashFixedApSsid[] = "Albert-FSD";
static constexpr char kDashFactoryApPassword[] = "12345678";
static constexpr char kDashFixedOtaUser[] = "admin";
static constexpr char kDashFixedOtaPassword[] = "12345678";
static constexpr char kDashApIdentityVersionKey[] = "apIdVer";
static constexpr uint8_t kDashApIdentityVersion = 1;
static constexpr size_t kDashMaxSsidLen = 32;
static constexpr size_t kDashMinApPassLen = 8;
static constexpr size_t kDashMaxPassLen = 64;
static constexpr int kDashApChannel = 1;
static constexpr int kDashApMaxConn = 4;
static uint8_t apRuntimeChannel = kDashApChannel;
static unsigned long apLastChannelSyncMs = 0;
static uint8_t apLastChannelSyncTarget = 0;
static bool apLastChannelSyncOk = false;

// WiFi STA (client) mode for internet access
static char staSSID[33] = "";
static char staPass[65] = "";
static bool staConnected = false;
static bool staConnectAttemptActive = false;
static bool staStaticIP = false;

// Multi-SSID storage
static constexpr uint8_t kDashMaxWifiNetworks = 4;
struct DashWifiNetwork
{
    char ssid[33];
    char pass[65];
    bool useStatic;
    char ip[16];
    char gw[16];
    char mask[16];
    char dns[16];
    uint8_t channel;
};
static DashWifiNetwork wifiNetworks[kDashMaxWifiNetworks] = {};
static uint8_t wifiNetworkCount = 0;
static int8_t wifiActiveSlot = -1;    // slot currently selected for STA attempt
static int8_t wifiNextRotateSlot = 0; // next slot to try when rotating
static unsigned long staConnectStartedAt = 0;
static unsigned long staRetryAt = 0;
static uint8_t staConsecutiveFailures = 0; // diagnostics only; retry interval is fixed
static bool staAttemptUsedSavedChannel = false;
static bool staForceFullScan = false;
static constexpr unsigned long kDashStaBootDelayMs = 1000;
static constexpr unsigned long kDashStaUnknownChannelBootDelayMs = 5000;
static constexpr unsigned long kDashStaDirectedRetryMs = 15000;
static constexpr unsigned long kDashStaSavedPollMs = 60000;
static constexpr unsigned long kDashStaConnectTimeoutMs = 10000;
// kDashStaRetryMs kept for backward compat with older references.
static constexpr unsigned long kDashStaRetryMs = kDashStaSavedPollMs;
static IPAddress staIP(0, 0, 0, 0);
static IPAddress staGW(0, 0, 0, 0);
static IPAddress staMask(255, 255, 255, 0);
static IPAddress staDNS(0, 0, 0, 0);

// Multi-SSID NVS helpers (key form: w0s, w0p, w0t, w0i, w0g, w0m, w0d)
static String dashWifiKey(uint8_t slot, const char *sub)
{
    String k = "w";
    k += slot;
    k += sub;
    return k;
}
static void dashClearWifiNetwork(DashWifiNetwork &n)
{
    n.ssid[0] = 0;
    n.pass[0] = 0;
    n.useStatic = false;
    n.ip[0] = 0;
    n.gw[0] = 0;
    n.mask[0] = 0;
    n.dns[0] = 0;
    n.channel = 0;
}
static void dashRotateAndConnect();
static void dashApplyRuntimeState();
static void dashClearRetiredOptionPrefs();
static void dashLog(const String &s);

#define LOG_CAP 80
struct LogEntry
{
    String msg;
    unsigned long seq;
};
static LogEntry logBuf[LOG_CAP];
static int logHead = 0;
static int logCount = 0;
static unsigned long logSeq = 0;
// Cursor tracking how much of logRing we have copied into logBuf so far.
// The Nag handler writes occasional debug diagnostics here when enablePrint is on.
static uint32_t logRingDrainCursor = 0;

static void dashLog(const String &s)
{
    logBuf[logHead] = {String(millis() / 1000) + "s " + s, ++logSeq};
    logHead = (logHead + 1) % LOG_CAP;
    if (logCount < LOG_CAP)
        logCount++;
    if (dashHandler && (bool)dashHandler->enablePrint)
        Serial.println(s);
}

// Pull all new entries from the per-frame handler logRing (in handlers.h)
// into logBuf so /log returns them. Cheap: bounded by ring capacity (32).
static void dashDrainLogRing()
{
    uint32_t h = logRing.currentHead();
    if (h <= logRingDrainCursor)
    {
        logRingDrainCursor = h; // handle wrap / restart
        return;
    }
    LogRingBuffer::Entry tmp[LogRingBuffer::kCapacity];
    int n = logRing.readSince(logRingDrainCursor, tmp, LogRingBuffer::kCapacity);
    for (int i = 0; i < n; i++)
    {
        // Use the timestamp captured at push time, not now, so messages keep
        // their actual ordering. dashLog format prefixes seconds-since-boot.
        logBuf[logHead] = {String(tmp[i].timestamp_ms / 1000) + "s " + String(tmp[i].msg), ++logSeq};
        logHead = (logHead + 1) % LOG_CAP;
        if (logCount < LOG_CAP)
            logCount++;
    }
    logRingDrainCursor = h;
}

// Public hooks
static void mcpDashOnFrame(const CanFrame &)
{
    unsigned long now = millis();
    rxCount++;
    lastFrameMs = now;
    canOnline = true;
    fpsFrames++;
}

static void mcpDashOnTxFrame(const CanFrame &, bool ok)
{
    txCount++;
    if (!ok)
        txErrCount++;
}

// JSON escape for log strings
static String jsonEscape(const String &s)
{
    String out;
    out.reserve(s.length() + 8);
    for (unsigned int i = 0; i < s.length(); i++)
    {
        char c = s.charAt(i);
        if (c == '"')
            out += "\\\"";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c < 0x20)
            out += ' ';
        else
            out += c;
    }
    return out;
}

static bool dashValidOtaTime(const String &value)
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

struct DashCanBootPolicy
{
    bool initialWriteEnabled = false;
    bool forcedOffAfterOta = false;
};

static DashCanBootPolicy dashPrepareCanBootPolicy()
{
    DashCanBootPolicy policy;
    Preferences bootPreferences;
    if (!bootPreferences.begin(PREFS_NS, false))
        return policy;

    const bool hadCanPreference = bootPreferences.isKey("can");
    bool configuredEnabled =
        bootPreferences.getBool("can", kDashInjectionDefaultEnabled);
    const bool explicitOtaMarker =
        bootPreferences.getBool(kDashOtaNagForceOffKey, false);

    const esp_partition_t *running = esp_ota_get_running_partition();
    const uint32_t runningAddress = running
                                        ? static_cast<uint32_t>(running->address)
                                        : 0U;
    const String runningVersion = dashFirmwareVersion();
    const bool hasFingerprint =
        bootPreferences.isKey(kDashCanFirmwareAddressKey) &&
        bootPreferences.isKey(kDashCanFirmwareVersionKey);
    const String runningAddressText(
        static_cast<unsigned long>(runningAddress));
    const String previousAddressText = bootPreferences.getString(
        kDashCanFirmwareAddressKey, runningAddressText.c_str());
    const uint32_t previousAddress = static_cast<uint32_t>(
        strtoul(previousAddressText.c_str(), nullptr, 10));
    const String previousVersion =
        bootPreferences.getString(kDashCanFirmwareVersionKey,
                                  runningVersion.c_str());
    const bool firmwareChanged = hasFingerprint &&
                                 (previousAddress != runningAddress ||
                                  previousVersion != runningVersion);
    // Existing V2.1 installations predate the fingerprint keys. A saved CAN
    // preference without a fingerprint is therefore treated as the first OTA
    // migration. A factory-erased first boot keeps the product default.
    const bool legacyUpgrade = hadCanPreference && !hasFingerprint;
    const bool forceOff = explicitOtaMarker || firmwareChanged || legacyUpgrade;

    const bool forcedStateChange = forceOff && configuredEnabled;
    if (forcedStateChange)
    {
        configuredEnabled = false;
        bootPreferences.putBool("can", false);
        bootPreferences.putBool(kDashNagBootRevisionKey, true);
    }

    bootPreferences.putString(kDashCanFirmwareAddressKey,
                              runningAddressText);
    bootPreferences.putString(kDashCanFirmwareVersionKey, runningVersion);
    bootPreferences.remove(kDashOtaNagForceOffKey);
    dashBootNagRevisionPending = forcedStateChange ||
                                 bootPreferences.getBool(
                                     kDashNagBootRevisionKey, false);
    bootPreferences.end();

    canActive = configuredEnabled;
    dashBootForcedNagOff = forceOff;
    policy.initialWriteEnabled = configuredEnabled;
    policy.forcedOffAfterOta = forceOff;
    return policy;
}

static bool dashMarkOtaNagForceOff()
{
    Preferences otaPreferences;
    if (!otaPreferences.begin(PREFS_NS, false))
        return false;
    otaPreferences.putBool(kDashOtaNagForceOffKey, true);
    otaPreferences.end();
    return true;
}

static void dashConsumeBootNagRevisionPending()
{
    if (!dashBootNagRevisionPending)
        return;
    Preferences bootPreferences;
    if (bootPreferences.begin(PREFS_NS, false))
    {
        bootPreferences.remove(kDashNagBootRevisionKey);
        bootPreferences.end();
    }
    dashBootNagRevisionPending = false;
}
#endif

static bool dashInjectionActive()
{
    return canActive;
}

#if defined(NAG_KILLER)
static uint32_t dashNagEchoCount()
{
    if (dashHandler)
        return (uint32_t)static_cast<NagHandler *>(dashHandler)->nagEchoCount;
    return 0;
}

static NagHandler *dashNagActiveHandler()
{
    return dashHandler ? static_cast<NagHandler *>(dashHandler) : nullptr;
}

static const char *dashNagModeName(uint8_t mode)
{
    return mode == NagHandler::MODE_A_V2 ? "A_V2" : "A";
}

static int16_t dashNagParseNmCenti(const String &value, int16_t fallback)
{
    char *end = nullptr;
    float parsed = strtof(value.c_str(), &end);
    if (end == value.c_str())
        return fallback;
    return NagHandler::nmToCentiNm(parsed);
}

static String dashNagNmString(int16_t centiNm)
{
    return String(NagHandler::centiNmToNm(centiNm), 2);
}

static bool dashApplyNagConfigArgs();

static String dashNagStatusJson(bool includeOk)
{
    NagHandler *nag = dashNagActiveHandler();
    String j = "{";
    if (includeOk)
        j += "\"ok\":true,";
    if (!nag)
    {
        j += "\"available\":false}";
        return j;
    }
    j += "\"available\":true";
    j += ",\"enabled\":";
    j += nagKillerEnabled ? "true" : "false";
    j += ",\"canWrite\":";
    j += canActive ? "true" : "false";
    j += ",\"mode\":";
    j += String((unsigned int)(uint8_t)nag->nagMode);
    j += ",\"modeName\":\"";
    j += dashNagModeName((uint8_t)nag->nagMode);
    j += "\",\"av2MinNm\":";
    j += dashNagNmString(nag->av2MinCenti());
    j += ",\"av2MaxNm\":";
    j += dashNagNmString(nag->av2MaxCenti());
    j += ",\"liveTorqueNm\":";
    j += dashNagNmString(nag->lastObservedCenti());
    j += ",\"lastTorqueNm\":";
    j += dashNagNmString(nag->lastInjectedCenti());
    j += ",\"echo\":";
    j += String(dashNagEchoCount());
    j += ",\"ownEchoSkip\":";
    j += String((uint32_t)nag->nagOwnEchoSkipCount);
    j += "}";
    return j;
}

#endif

static void dashPostProcessFrame(const CanFrame &original, CanDriver &driver)
{
    (void)original;
    (void)driver;
}

static bool dashStaSsidLooksCorrupt(const String &ssid)
{
    return ssid.indexOf("\"ssid\"") >= 0 || ssid.indexOf("{\"") >= 0 ||
           ssid.indexOf("\",\"") >= 0;
}

static void dashApplyRuntimeState()
{
#if defined(NAG_KILLER)
    nagKillerRuntime = nagKillerEnabled && canActive &&
                       appCanTransmitRuntimeReady();
#else
    nagKillerRuntime = false;
#endif

#if defined(DASH_RGB_STATUS_LED)
    appRefreshStatusLed();
#endif
}

// Store config
static void dashSavePrefs()
{
    prefs.begin(PREFS_NS, false);
    prefs.putUChar("hw", hwMode);
    prefs.putUChar("hw_def", DASH_DEFAULT_HW);
    prefs.putBool("can", canActive);
    prefs.putBool("force_act", false);
    prefs.putBool("ap_rst", false);
    prefs.putBool("ap_gate", false);
#if defined(NAG_KILLER)
    nagKillerEnabled = true;
    prefs.putBool("nag_en", true);
    if (NagHandler *nag = dashNagActiveHandler())
    {
        prefs.putUChar("nag_mode", (uint8_t)nag->nagMode);
        prefs.putString("nag_av2_min", dashNagNmString(nag->av2MinCenti()));
        prefs.putString("nag_av2_max", dashNagNmString(nag->av2MaxCenti()));
    }
#endif
    prefs.putBool("auto_sleep", false);
    prefs.putBool("sp_auto", true);
    prefs.putUChar("sp_sel", 1);
    prefs.putBool("eprn", dashHandler ? (bool)dashHandler->enablePrint : false);
    prefs.end();
}

static void dashSetCanActive(bool active, const char *reason = nullptr)
{
    bool changed = canActive != active;
    canActive = active;
    dashApplyRuntimeState();
    dashSavePrefs();
    if (changed)
    {
        String msg = String("[CFG] Nag/CAN TX ") + (active ? "ON" : "OFF");
        if (reason && *reason)
            msg += String(" via ") + reason;
        dashLog(msg);
    }
}

[[maybe_unused]] static void dashToggleCanActive(const char *reason = nullptr)
{
    dashSetCanActive(!canActive, reason);
}

static bool dashApPasswordLengthValid(size_t len)
{
    return len >= kDashMinApPassLen && len <= kDashMaxPassLen;
}

static bool dashApConfigValid(const char *ssid, const char *pass)
{
    size_t ssidLen = strlen(ssid);
    size_t passLen = strlen(pass);
    return ssidLen > 0 && ssidLen <= kDashMaxSsidLen && dashApPasswordLengthValid(passLen);
}

#if defined(ESP_PLATFORM) && defined(DASH_WIFI_PERF_TUNING)
static void dashApplyWifiPerfTuning()
{
    static bool logged = false;
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW20);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_max_tx_power(78); // 19.5 dBm, within ESP-IDF's quarter-dBm scale.
    if (!logged)
    {
        logged = true;
        dashLog("[WIFI] WIFI-NAG radio tuning: HT20, AP 11g/n, STA 11b/g/n, max TX power");
    }
}
#else
static void dashApplyWifiPerfTuning() {}
#endif

static void dashUseDefaultApConfig()
{
    strlcpy(apSSID, kDashFixedApSsid, sizeof(apSSID));
    strlcpy(apPass, kDashFactoryApPassword, sizeof(apPass));
    apHidden = false;
    apRuntimeChannel = kDashApChannel;
}

static uint8_t dashConfiguredApChannel()
{
    return kDashApChannel;
}

static bool dashStaConfigLengthValid(const String &ssid, const String &pass)
{
    return ssid.length() <= kDashMaxSsidLen && pass.length() <= kDashMaxPassLen;
}

static void dashClearRetiredOptionPrefs()
{
    static const char *const keys[] = {
        "fAD",
        "f_AD",
        "f_nag",
        "f_sum",
        "f_isa",
        "f_evd",
        "f_h4o",
        "sp",
        "sp_lock",
    };

    bool removed = false;
    for (const char *key : keys)
    {
        if (!prefs.isKey(key))
            continue;
        prefs.remove(key);
        removed = true;
    }

    if (removed)
        dashLog("[BOOT] Cleared retired dashboard prefs from NVS");
}

static void dashLoadPrefs()
{
    prefs.begin(PREFS_NS, false);
    lastOtaUploadTime = prefs.getString("ota_time", "");
    if (!lastOtaUploadTime.isEmpty() && !dashValidOtaTime(lastOtaUploadTime))
    {
        prefs.remove("ota_time");
        lastOtaUploadTime = "";
    }
    uint8_t storedHw = prefs.getUChar("hw", DASH_DEFAULT_HW);
    uint8_t storedDefaultHw = prefs.getUChar("hw_def", kDashUnsetU8);
    bool migratedHw = storedHw != DASH_DEFAULT_HW || storedDefaultHw != DASH_DEFAULT_HW;
    dashClearRetiredOptionPrefs();

    hwMode = DASH_DEFAULT_HW;
    if (storedHw != DASH_DEFAULT_HW)
        prefs.putUChar("hw", hwMode);
    if (storedDefaultHw != DASH_DEFAULT_HW)
        prefs.putUChar("hw_def", DASH_DEFAULT_HW);
    canActive = prefs.getBool("can", kDashInjectionDefaultEnabled);
    // The early boot policy also selects the TWAI hardware mode. Never let a
    // later preference reload undo an OTA force-off and trigger a live
    // controller reinstall. A manual WebUI/BLE command may enable it later.
    if (dashBootForcedNagOff)
        canActive = false;
    if (prefs.getBool("force_act", false))
        prefs.putBool("force_act", false);
    if (prefs.getBool("ap_rst", false))
        prefs.putBool("ap_rst", false);
    if (prefs.getBool("ap_gate", false))
        prefs.putBool("ap_gate", false);
#if defined(NAG_KILLER)
    nagKillerEnabled = true;
    if (!prefs.getBool("nag_en", true))
        prefs.putBool("nag_en", true);
    if (NagHandler *nag = dashNagActiveHandler())
    {
        int16_t minNm = dashNagParseNmCenti(prefs.getString("nag_av2_min", "1.50"), 150);
        int16_t maxNm = dashNagParseNmCenti(prefs.getString("nag_av2_max", "1.80"), 180);
        nag->setAv2RangeCentiNm(minNm, maxNm);
        nag->setMode(prefs.getUChar("nag_mode", NagHandler::MODE_A));
    }
#endif
    if (prefs.getBool("auto_sleep", false))
        prefs.putBool("auto_sleep", false);
    if (!prefs.getBool("sp_auto", true))
        prefs.putBool("sp_auto", true);
    if (prefs.getUChar("sp_sel", 1) != 1)
        prefs.putUChar("sp_sel", 1);
    bool ep = prefs.getBool("eprn", false);

    dashApplyRuntimeState();
    if (dashHandler)
        dashHandler->enablePrint = ep;
    // Lock the product hotspot identity. The one-shot migration also puts old
    // installations onto the shared factory password; later password changes
    // remain user-controlled.
    const uint8_t apIdentityVersion =
        prefs.getUChar(kDashApIdentityVersionKey, 0);
    if (apIdentityVersion < kDashApIdentityVersion)
    {
        prefs.remove("ap_ssid");
        prefs.putString("ap_pass", kDashFactoryApPassword);
        prefs.putUChar(kDashApIdentityVersionKey,
                       kDashApIdentityVersion);
        dashLog("[WIFI] AP identity migrated to Albert-FSD");
    }

    // Load the configurable WiFi AP password/visibility.
    String apPassPref = prefs.isKey("ap_pass") ? prefs.getString("ap_pass", "") : "";
    bool hasApOverride = apPassPref.length() > 0 || prefs.isKey("ap_hidden");
    bool invalidApOverride =
        apPassPref.length() > 0 &&
        !dashApPasswordLengthValid(apPassPref.length());
    strlcpy(apSSID, kDashFixedApSsid, sizeof(apSSID));
    if (apPassPref.length() > 0)
        strlcpy(apPass, apPassPref.c_str(), sizeof(apPass));
    else
        strlcpy(apPass, kDashFactoryApPassword, sizeof(apPass));
    apHidden = prefs.getBool("ap_hidden", false);
    if (invalidApOverride || !dashApConfigValid(apSSID, apPass))
    {
        if (hasApOverride)
        {
            prefs.remove("ap_pass");
            prefs.remove("ap_hidden");
            dashLog("[WIFI] Invalid saved AP config ignored");
        }
        dashUseDefaultApConfig();
    }
    apRuntimeChannel = dashConfiguredApChannel();

    // Load WiFi STA networks (multi-SSID slot array)
    wifiNetworkCount = 0;
    for (uint8_t i = 0; i < kDashMaxWifiNetworks; i++)
        dashClearWifiNetwork(wifiNetworks[i]);

    uint8_t storedCount = prefs.getUChar("wn_cnt", 0);
    if (storedCount > kDashMaxWifiNetworks)
        storedCount = kDashMaxWifiNetworks;

    for (uint8_t i = 0; i < storedCount; i++)
    {
        DashWifiNetwork &n = wifiNetworks[wifiNetworkCount];
        String s = prefs.getString(dashWifiKey(i, "s").c_str(), "");
        String p = prefs.getString(dashWifiKey(i, "p").c_str(), "");
        if (!dashStaConfigLengthValid(s, p) || dashStaSsidLooksCorrupt(s) || s.length() == 0)
            continue;
        strlcpy(n.ssid, s.c_str(), sizeof(n.ssid));
        strlcpy(n.pass, p.c_str(), sizeof(n.pass));
        n.channel = prefs.getUChar(dashWifiKey(i, "c").c_str(), 0);
        n.useStatic = prefs.getBool(dashWifiKey(i, "t").c_str(), false);
        if (n.useStatic)
        {
            String ip = prefs.getString(dashWifiKey(i, "i").c_str(), "0.0.0.0");
            String gw = prefs.getString(dashWifiKey(i, "g").c_str(), "0.0.0.0");
            String mk = prefs.getString(dashWifiKey(i, "m").c_str(), "255.255.255.0");
            String dn = prefs.getString(dashWifiKey(i, "d").c_str(), "0.0.0.0");
            strlcpy(n.ip, ip.c_str(), sizeof(n.ip));
            strlcpy(n.gw, gw.c_str(), sizeof(n.gw));
            strlcpy(n.mask, mk.c_str(), sizeof(n.mask));
            strlcpy(n.dns, dn.c_str(), sizeof(n.dns));
        }
        wifiNetworkCount++;
    }

    // One-shot migration from legacy single-SSID keys
    if (wifiNetworkCount == 0 && prefs.isKey("wifi_ssid"))
    {
        String s = prefs.getString("wifi_ssid", "");
        String p = prefs.getString("wifi_pass", "");
        if (dashStaConfigLengthValid(s, p) && !dashStaSsidLooksCorrupt(s) && s.length() > 0)
        {
            DashWifiNetwork &n = wifiNetworks[0];
            strlcpy(n.ssid, s.c_str(), sizeof(n.ssid));
            strlcpy(n.pass, p.c_str(), sizeof(n.pass));
            n.useStatic = prefs.getBool("wifi_static", false);
            if (n.useStatic)
            {
                strlcpy(n.ip, prefs.getString("wifi_ip", "0.0.0.0").c_str(), sizeof(n.ip));
                strlcpy(n.gw, prefs.getString("wifi_gw", "0.0.0.0").c_str(), sizeof(n.gw));
                strlcpy(n.mask, prefs.getString("wifi_mask", "255.255.255.0").c_str(), sizeof(n.mask));
                strlcpy(n.dns, prefs.getString("wifi_dns", "0.0.0.0").c_str(), sizeof(n.dns));
            }
            wifiNetworkCount = 1;
            prefs.putUChar("wn_cnt", 1);
            prefs.putString(dashWifiKey(0, "s").c_str(), s);
            prefs.putString(dashWifiKey(0, "p").c_str(), p);
            prefs.putBool(dashWifiKey(0, "t").c_str(), n.useStatic);
            if (n.useStatic)
            {
                prefs.putString(dashWifiKey(0, "i").c_str(), String(n.ip));
                prefs.putString(dashWifiKey(0, "g").c_str(), String(n.gw));
                prefs.putString(dashWifiKey(0, "m").c_str(), String(n.mask));
                prefs.putString(dashWifiKey(0, "d").c_str(), String(n.dns));
            }
            dashLog("[WIFI] Migrated legacy STA config to slot 0");
        }
        prefs.remove("wifi_ssid");
        prefs.remove("wifi_pass");
        prefs.remove("wifi_static");
        prefs.remove("wifi_ip");
        prefs.remove("wifi_gw");
        prefs.remove("wifi_mask");
        prefs.remove("wifi_dns");
    }

    // Seed staSSID/staPass with the preferred slot (last network that
    // successfully connected) if it's still valid, otherwise fall back to
    // slot 0. Connecting to the last-known-good network first is much faster
    // than always rotating from slot 0 across reboots.
    if (wifiNetworkCount > 0)
    {
        uint8_t preferred = prefs.getUChar("wn_pref", 0);
        if (preferred >= wifiNetworkCount)
            preferred = 0;
        const DashWifiNetwork &n = wifiNetworks[preferred];
        strlcpy(staSSID, n.ssid, sizeof(staSSID));
        strlcpy(staPass, n.pass, sizeof(staPass));
        staStaticIP = n.useStatic;
        if (n.useStatic)
        {
            staIP.fromString(n.ip);
            staGW.fromString(n.gw);
            staMask.fromString(n.mask);
            staDNS.fromString(n.dns);
        }
        wifiActiveSlot = static_cast<int8_t>(preferred);
        wifiNextRotateSlot = preferred;
    }
    else
    {
        staSSID[0] = 0;
        staPass[0] = 0;
        staStaticIP = false;
        wifiActiveSlot = -1;
        wifiNextRotateSlot = 0;
    }

    prefs.end();

    if (migratedHw)
        dashLog("[BOOT] HW locked to WIFI-NAG ESP32-S3 TWAI");
    dashLog("[BOOT] Prefs loaded product=WIFI-NAG hw=" + String(hwMode));
    dashLog("[BOOT] canActive=" + String(canActive ? "YES" : "NO"));
}

static WebServer server(80);

#include "web/dash_gateway.h"

static DashMemoryDiagnostics dashMemoryDiagnostics;

static DashDiagRuntimeState dashBuildDiagnosticsRuntimeState()
{
    DashDiagRuntimeState state;
    wifi_mode_t wifiMode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&wifiMode);
    state.wifiMode = static_cast<uint8_t>(wifiMode);
    state.wifiStatus = static_cast<uint8_t>(WiFi.status());
    state.apClients = WiFi.softAPgetStationNum();
    wifi_ap_record_t apInfo = {};
    if (esp_wifi_sta_get_ap_info(&apInfo) == ESP_OK)
        state.wifiRssi = apInfo.rssi;
#if defined(BLE_BRIDGE)
    const BleBridgeDiagnostics ble = bleBridgeClient.diagnostics();
    state.bleProtocol = static_cast<uint8_t>(ble.protocolStatus);
    state.bleFlags = (ble.enabled ? 0x01U : 0U) |
                     (ble.scanning ? 0x02U : 0U) |
                     (ble.connecting ? 0x04U : 0U) |
                     (ble.connected ? 0x08U : 0U) |
                     (ble.bridgeReady ? 0x10U : 0U) |
                     (ble.pairing ? 0x20U : 0U);
    state.bleRssi = ble.rssi;
    state.bleDisconnectReason = ble.lastDisconnectReason;
    state.bleReconnectCount = ble.reconnectCount;
    state.bleDisconnectCount = ble.disconnectCount;
#endif
#if defined(DASH_STA_AP_GATEWAY)
    state.gatewayPending = dashGatewayPendingCount();
    state.gatewayPendingMax = gatewayDnsPendingMax;
    state.gatewayPendingFull = gatewayDnsPendingFull;
    state.gatewayTimeouts = gatewayDnsTimeouts;
#endif
    state.rxCount = static_cast<uint32_t>(rxCount);
    state.txCount = static_cast<uint32_t>(txCount);
    state.txErrorCount = static_cast<uint32_t>(txErrCount);
    state.otaRunning = Update.isRunning();
    state.canOnline = canOnline;
    state.canWriteEnabled = canActive;
    return state;
}

#if defined(NAG_KILLER)
static bool dashApplyNagConfigArgs()
{
    NagHandler *nag = dashNagActiveHandler();
    if (!nag)
        return false;

    bool changed = false;
    if (server.hasArg("nagMode") || server.hasArg("m"))
    {
        uint8_t requested = static_cast<uint8_t>((server.hasArg("nagMode") ? server.arg("nagMode") : server.arg("m")).toInt());
        if (!NagHandler::isSupportedMode(requested))
            requested = NagHandler::MODE_A;
        if ((uint8_t)nag->nagMode != requested)
        {
            nag->setMode(requested);
            changed = true;
        }
    }

    int16_t minNm = nag->av2MinCenti();
    int16_t maxNm = nag->av2MaxCenti();
    bool rangeChanged = false;
    if (server.hasArg("av2MinNm") || server.hasArg("av2Min"))
    {
        minNm = dashNagParseNmCenti(server.hasArg("av2MinNm") ? server.arg("av2MinNm") : server.arg("av2Min"), minNm);
        rangeChanged = true;
    }
    if (server.hasArg("av2MaxNm") || server.hasArg("av2Max"))
    {
        maxNm = dashNagParseNmCenti(server.hasArg("av2MaxNm") ? server.arg("av2MaxNm") : server.arg("av2Max"), maxNm);
        rangeChanged = true;
    }
    if (rangeChanged)
    {
        int16_t oldMin = nag->av2MinCenti();
        int16_t oldMax = nag->av2MaxCenti();
        nag->setAv2RangeCentiNm(minNm, maxNm);
        changed = changed || oldMin != nag->av2MinCenti() || oldMax != nag->av2MaxCenti();
    }

    if (changed)
    {
        dashLog("[CFG] Nag mode=" + String(dashNagModeName((uint8_t)nag->nagMode)) +
                " A_V2=" + dashNagNmString(nag->av2MinCenti()) + ".." + dashNagNmString(nag->av2MaxCenti()) + " Nm");
    }
    return changed;
}
#endif

static void handleRoot()
{
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
#ifdef ESP_PLATFORM
    server.sendRaw(200, "text/html",
                   reinterpret_cast<const char *>(DASH_HTML_GZ),
                   DASH_HTML_GZ_LEN);
#else
    server.send_P(200, "text/html", reinterpret_cast<const char *>(DASH_HTML_GZ), DASH_HTML_GZ_LEN);
#endif
}

static void handleLegacyDashboardRedirect()
{
    server.sendHeader("Location", "/");
    server.sendHeader("Cache-Control", "no-store");
    server.send(302, "text/plain", "Moved");
}

static void handleStatus()
{
    if (canOnline && millis() - lastFrameMs > 10000)
    {
        canOnline = false;
        dashLog("[CAN] Bus OFFLINE (timeout)");
    }
    unsigned long now = millis();
    if (now - fpsLastMs >= 2000)
    {
        fps = fpsFrames * 1000.0f / max(1UL, now - fpsLastMs);
        fpsFrames = 0;
        fpsLastMs = now;
    }

    bool ep = dashHandler ? (bool)dashHandler->enablePrint : false;

    String j = "{\"product\":\"wifi-nag\"";
    j.reserve(900);
    j += ",\"wifiNag\":true";
#if defined(NAG_KILLER)
    j += ",\"nagKiller\":";
    j += nagKillerEnabled ? "true" : "false";
    j += ",\"nagEcho\":";
    j += String(dashNagEchoCount());
    if (NagHandler *nag = dashNagActiveHandler())
    {
        j += ",\"nagMode\":";
        j += String((unsigned int)(uint8_t)nag->nagMode);
        j += ",\"nagModeName\":\"";
        j += dashNagModeName((uint8_t)nag->nagMode);
        j += "\",\"nagAv2MinNm\":";
        j += dashNagNmString(nag->av2MinCenti());
        j += ",\"nagAv2MaxNm\":";
        j += dashNagNmString(nag->av2MaxCenti());
        j += ",\"nagLiveTorqueNm\":";
        j += dashNagNmString(nag->lastObservedCenti());
        j += ",\"nagLastTorqueNm\":";
        j += dashNagNmString(nag->lastInjectedCenti());
        j += ",\"nagOwnEchoSkip\":";
        j += String((uint32_t)nag->nagOwnEchoSkipCount);
    }
#endif
    j += ",\"hw\":";
    j += hwMode;
    j += ",\"eprn\":";
    j += ep ? "true" : "false";
    j += ",\"ia\":";
    j += dashInjectionActive() ? "true" : "false";
    j += ",\"can\":";
    j += canOnline ? "true" : "false";
    j += ",\"ci\":";
    j += canActive ? "true" : "false";
    j += ",\"rx\":";
    j += rxCount;
    j += ",\"tx\":";
    j += txCount;
    j += ",\"txerr\":";
    j += txErrCount;
    j += ",\"fps\":";
    {
        unsigned long fpsX10 = static_cast<unsigned long>(fps * 10.0f + 0.5f);
        j += String(fpsX10 / 10);
        j += ".";
        j += String(fpsX10 % 10);
    }
    j += ",\"up\":";
    j += (millis() - startMs) / 1000;
    j += "}";
    server.send(200, "application/json", j);
}

static void handleConfig()
{
    if (server.hasArg("can") || server.hasArg("force"))
    {
        bool requestedTx = server.hasArg("can") ? (server.arg("can") == "1") : (server.arg("force") == "1");
        if (requestedTx != canActive)
        {
            canActive = requestedTx;
            dashLog("[CFG] Nag/CAN TX " + String(requestedTx ? "ON" : "OFF"));
        }
    }
#if defined(NAG_KILLER)
    nagKillerEnabled = true;
    dashApplyNagConfigArgs();
#endif
    dashApplyRuntimeState();
    dashSavePrefs();
    server.send(200, "application/json", "{\"ok\":true,\"product\":\"wifi-nag\"}");
}
#if defined(NAG_KILLER) && defined(PRODUCT_WIFI_NAG)
static void handleNagApiConfig()
{
    server.send(200, "application/json", dashNagStatusJson(false));
}

static void handleNagApiStats()
{
    server.send(200, "application/json", dashNagStatusJson(false));
}

static void handleNagApiMode()
{
    dashApplyNagConfigArgs();
    dashApplyRuntimeState();
    dashSavePrefs();
    server.send(200, "application/json", dashNagStatusJson(true));
}

static void handleNagApiUpdate()
{
    dashApplyNagConfigArgs();
    dashApplyRuntimeState();
    dashSavePrefs();
    server.send(200, "application/json", dashNagStatusJson(true));
}
#endif

static void handleLoggingConfig()
{
    if (server.hasArg("eprn") && dashHandler)
    {
        bool ep = server.arg("eprn") == "1";
        dashHandler->enablePrint = ep;
        dashLog("[CFG] Logging " + String(ep ? "ON" : "OFF"));
    }
    dashApplyRuntimeState();
    dashSavePrefs();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleLog()
{
    // Pick up any new per-frame handler diagnostics first.
    dashDrainLogRing();
    unsigned long since = 0;
    if (server.hasArg("since"))
        since = strtoul(server.arg("since").c_str(), nullptr, 10);
    String j = "{\"seq\":";
    j.reserve(256 + static_cast<size_t>(logCount) * 96);
    j += logSeq;
    j += ",\"lines\":[";
    int start = (logCount < LOG_CAP) ? 0 : logHead;
    int count = min(logCount, LOG_CAP);
    bool first = true;
    for (int i = 0; i < count; i++)
    {
        int idx = (start + i) % LOG_CAP;
        if (logBuf[idx].seq <= since)
            continue;
        if (!first)
            j += ",";
        first = false;
        j += "\"" + jsonEscape(logBuf[idx].msg) + "\"";
    }
    j += "]}";
    server.send(200, "application/json", j);
}

static void handleDisable()
{
    dashSetCanActive(false, "dashboard");
    server.send(200, "text/plain", "Injection stopped.");
}

static void dashScheduleRestart(uint32_t delayMs)
{
    dashRestartAtMs = millis() + delayMs;
    dashRestartPending = true;
}

static void handleReboot()
{
    dashMemoryDiagnostics.recordEvent(DASH_DIAG_EVENT_RESTART_REQUESTED,
                                      dashBuildDiagnosticsRuntimeState());
    server.send(200, "text/plain", "Rebooting...");
    dashScheduleRestart(500);
}

static void handleOtaResult()
{
    if (!server.authenticate(kDashFixedOtaUser, kDashFixedOtaPassword))
    {
        server.requestAuthentication();
        return;
    }
    bool ok = Update.isFinished() && !Update.hasError();
    server.sendHeader("Connection", "close");
    server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
    if (ok)
    {
        dashMemoryDiagnostics.recordEvent(DASH_DIAG_EVENT_OTA_END,
                                          dashBuildDiagnosticsRuntimeState());
        dashMarkOtaNagForceOff();
        dashOtaCanPrepared = false;
        dashLog("[OTA] Upload complete -- restart scheduled");
        // Let the HTTP handler return and give the TCP response time to reach
        // the browser. CAN queue draining and ESP.restart() run later in the
        // dashboard task, never inside the OTA request callback.
        dashScheduleRestart(1200);
    }
    else
    {
        dashMemoryDiagnostics.recordEvent(DASH_DIAG_EVENT_OTA_FAIL,
                                          dashBuildDiagnosticsRuntimeState());
        dashLog("[OTA] Upload FAILED: " + String(Update.errorString()));
        Update.abort();
        dashOtaCanPrepared = false;
        appResumeCanAfterOtaFailure();
    }
}

static void handleOtaUpload()
{
    if (!server.authenticate(kDashFixedOtaUser, kDashFixedOtaPassword))
        return;
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START)
    {
        dashMemoryDiagnostics.recordEvent(DASH_DIAG_EVENT_OTA_START,
                                          dashBuildDiagnosticsRuntimeState());
        dashLog("[OTA] Receiving: " + String(upload.filename.c_str()));
        dashOtaCanPrepared = appPrepareCanForOta();
        if (!dashOtaCanPrepared)
        {
            dashLog("[OTA] Restart already pending; upload rejected");
            Update.abort();
            return;
        }
        esp_task_wdt_deinit();
        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
        {
            dashMemoryDiagnostics.recordEvent(DASH_DIAG_EVENT_OTA_FAIL,
                                              dashBuildDiagnosticsRuntimeState());
            dashLog("[OTA] Begin failed: " + String(Update.errorString()));
            dashOtaCanPrepared = false;
            appResumeCanAfterOtaFailure();
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (!dashOtaCanPrepared)
            return;
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
        {
            dashMemoryDiagnostics.recordEvent(DASH_DIAG_EVENT_OTA_FAIL,
                                              dashBuildDiagnosticsRuntimeState(),
                                              static_cast<uint32_t>(upload.currentSize));
            dashLog("[OTA] Write error: " + String(Update.errorString()));
            Update.abort();
            dashOtaCanPrepared = false;
            appResumeCanAfterOtaFailure();
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (upload.totalSize > 0 && Update.end(true) && Update.isFinished())
        {
            dashMemoryDiagnostics.recordEvent(DASH_DIAG_EVENT_OTA_END,
                                              dashBuildDiagnosticsRuntimeState(),
                                              static_cast<uint32_t>(upload.totalSize));
            dashPersistOtaTime(server.arg("ota_time"));
            dashMarkOtaNagForceOff();
            dashLog("[OTA] Done: " + String(upload.totalSize) + " bytes");
        }
        else
        {
            dashMemoryDiagnostics.recordEvent(DASH_DIAG_EVENT_OTA_FAIL,
                                              dashBuildDiagnosticsRuntimeState(),
                                              static_cast<uint32_t>(upload.totalSize));
            dashLog("[OTA] End failed: " + String(Update.errorString()));
            Update.abort();
            dashOtaCanPrepared = false;
            appResumeCanAfterOtaFailure();
        }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
        dashMemoryDiagnostics.recordEvent(DASH_DIAG_EVENT_OTA_FAIL,
                                          dashBuildDiagnosticsRuntimeState(),
                                          static_cast<uint32_t>(upload.totalSize));
        dashLog("[OTA] Upload aborted");
        Update.abort();
        dashOtaCanPrepared = false;
        appResumeCanAfterOtaFailure();
    }
}

// CAN RUNTIME MANAGEMENT

// ── WIFI STA ────────────────────────────────────────────────────

static bool dashStartAccessPoint(bool withSta)
{
    WiFi.persistent(false);
    WiFi.mode(withSta ? WIFI_AP_STA : WIFI_AP);
    WiFi.setSleep(false);
    dashApplyWifiPerfTuning();

    IPAddress apIp(100, 100, 1, 1);
    IPAddress apMask(255, 255, 255, 0);
    WiFi.softAPConfig(apIp, apIp, apMask);

    if (!dashApConfigValid(apSSID, apPass))
        dashUseDefaultApConfig();

    apRuntimeChannel = dashConfiguredApChannel();
    bool ok = WiFi.softAP(apSSID, apPass, apRuntimeChannel, apHidden ? 1 : 0, kDashApMaxConn);
    if (!ok)
    {
        dashUseDefaultApConfig();
        ok = WiFi.softAP(apSSID, apPass, apRuntimeChannel, 0, kDashApMaxConn);
    }
    if (!ok)
        dashLog("[WIFI] AP start failed");
    else
        dashGatewayOnApStarted(WiFi.apNetif());
    return ok;
}

static void dashBeginSTA()
{
    if (strlen(staSSID) == 0)
        return;

    // Ensure STA interface is enabled (AP+STA) before initiating a STA connect.
    // Without this, esp_wifi_set_config(WIFI_IF_STA, ...) inside WiFi.begin()
    // can fail silently when the device is in AP-only mode.
    if (WiFi.getMode() != WIFI_AP_STA)
    {
        WiFi.mode(WIFI_AP_STA);
        dashApplyWifiPerfTuning();
    }

    if (staStaticIP && (uint32_t)staIP != 0)
    {
        WiFi.config(staIP, staGW, staMask, staDNS);
        dashLog("[WIFI] Static IP: " + staIP.toString());
    }
    else
    {
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }
    // Disconnect any prior STA association before issuing a fresh begin().
    // Back-to-back WiFi.begin() without disconnect can leave esp_wifi in an
    // intermediate state and trigger an extra channel switch on the shared
    // AP+STA radio, which the AP beacon picks up as jitter (clients on
    // 100.100.1.1 momentarily see the AP go away). eraseAP=false keeps the
    // soft-AP up; wifioff=false keeps the radio on.
    WiFi.disconnect(false, false);
    uint8_t channel = 0;
    if (!staForceFullScan && wifiActiveSlot >= 0 &&
        wifiActiveSlot < static_cast<int8_t>(wifiNetworkCount))
    {
        const uint8_t savedChannel = wifiNetworks[wifiActiveSlot].channel;
        if (savedChannel >= 1 && savedChannel <= 14)
            channel = savedChannel;
    }
    staAttemptUsedSavedChannel = channel != 0;
    staForceFullScan = false;
    WiFi.begin(staSSID, staPass, channel);
    staConnectAttemptActive = true;
    staConnectStartedAt = millis();
    staRetryAt = 0;
    if (channel)
        dashLog("[WIFI] Connecting to " + String(staSSID) +
                " on saved CH" + String(channel) + "...");
    else
        dashLog("[WIFI] Connecting to " + String(staSSID) + " with full scan...");
}

static void dashPrepareStaReconnect()
{
    if (staConnectAttemptActive || staConnected || WiFi.status() == WL_CONNECTED)
        WiFi.disconnect(false, false);
    dashGatewayOnStaDisconnected(WiFi.apNetif());
    staConnected = false;
    staConnectAttemptActive = false;
    staRetryAt = 0;
    staConsecutiveFailures = 0; // user-initiated reconnect resets diagnostics
    staAttemptUsedSavedChannel = false;
    staForceFullScan = false;
}

static void dashApplyWifiSlot(uint8_t slot)
{
    if (slot >= wifiNetworkCount)
        return;
    const DashWifiNetwork &n = wifiNetworks[slot];
    strlcpy(staSSID, n.ssid, sizeof(staSSID));
    strlcpy(staPass, n.pass, sizeof(staPass));
    staStaticIP = n.useStatic;
    if (n.useStatic)
    {
        staIP.fromString(n.ip);
        staGW.fromString(n.gw);
        staMask.fromString(n.mask);
        staDNS.fromString(n.dns);
    }
    else
    {
        staIP = IPAddress(0, 0, 0, 0);
    }
    wifiActiveSlot = static_cast<int8_t>(slot);
}

static void dashRotateAndConnect()
{
    if (wifiNetworkCount == 0)
        return;
    // Rotate through saved slots, skipping any slot whose SSID matches our own AP
    // (connecting to ourselves would bring down the AP and disconnect all clients).
    for (uint8_t tries = 0; tries < wifiNetworkCount; tries++)
    {
        uint8_t next = wifiNextRotateSlot % wifiNetworkCount;
        wifiNextRotateSlot = (next + 1) % wifiNetworkCount;
        dashApplyWifiSlot(next);
        if (strlen(apSSID) > 0 && strcmp(staSSID, apSSID) == 0)
        {
            dashLog("[WIFI] Skipping slot " + String(next) + " (matches own AP SSID)");
            continue;
        }
        dashLog("[WIFI] Trying slot " + String(next) + ": " + String(staSSID));
        // AP is already running in AP+STA mode since boot; don't restart it.
        // Only re-assert AP_STA mode if it was actually changed, to avoid
        // unnecessary esp_wifi_set_mode() calls that can briefly disturb the
        // shared AP+STA radio. dashBeginSTA() also sets AP_STA defensively.
        if (WiFi.getMode() != WIFI_AP_STA)
            WiFi.mode(WIFI_AP_STA);
        dashBeginSTA();
        return;
    }
    dashLog("[WIFI] No connectable STA slots (all match own AP SSID?)");
}

static void dashScheduleSTAConnect(unsigned long delayMs)
{
    if (strlen(staSSID) == 0)
        return;
    staConnectAttemptActive = false;
    staRetryAt = millis() + delayMs;
}

static void dashPrepareWifiScan()
{
    if (WiFi.getMode() != WIFI_AP_STA)
    {
        WiFi.mode(WIFI_AP_STA);
        dashApplyWifiPerfTuning();
    }
    WiFi.setSleep(false);
}

static uint8_t dashCurrentApChannel()
{
#ifdef ESP_PLATFORM
    wifi_config_t cfg = {};
    if (esp_wifi_get_config(WIFI_IF_AP, &cfg) == ESP_OK && cfg.ap.channel > 0)
        return cfg.ap.channel;
#endif
    return apRuntimeChannel;
}

static bool dashSyncApChannelToSta()
{
#ifdef ESP_PLATFORM
    wifi_ap_record_t staInfo = {};
    if (esp_wifi_sta_get_ap_info(&staInfo) != ESP_OK || staInfo.primary == 0)
        return false;

    uint8_t staChannel = staInfo.primary;
    uint8_t apChannel = dashCurrentApChannel();
    apLastChannelSyncTarget = staChannel;
    apLastChannelSyncMs = millis();

    if (apChannel == staChannel)
    {
        apRuntimeChannel = staChannel;
        apLastChannelSyncOk = true;
        dashLog("[WIFI] AP channel already matches STA CH" + String(staChannel));
        return true;
    }

    wifi_config_t cfg = {};
    esp_err_t err = esp_wifi_get_config(WIFI_IF_AP, &cfg);
    if (err == ESP_OK)
    {
        cfg.ap.channel = staChannel;
        err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    }

    if (err == ESP_OK)
    {
        apRuntimeChannel = staChannel;
        apLastChannelSyncOk = true;
        dashLog("[WIFI] AP channel auto matched: AP CH" + String(apChannel) +
                " -> STA CH" + String(staChannel));
        return true;
    }

    apLastChannelSyncOk = false;
    dashLog("[WIFI] AP channel auto match failed: AP CH" + String(apChannel) +
            " STA CH" + String(staChannel) + " err=" + String(esp_err_to_name(err)));
#endif
    return false;
}

static const char *dashWifiStatusName(int status)
{
    switch (status)
    {
    case WL_IDLE_STATUS:
        return "IDLE";
    case WL_NO_SSID_AVAIL:
        return "NO_SSID";
    case WL_SCAN_COMPLETED:
        return "SCAN_DONE";
    case WL_CONNECTED:
        return "CONNECTED";
    case WL_CONNECT_FAILED:
        return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:
        return "CONNECTION_LOST";
    case WL_DISCONNECTED:
        return "DISCONNECTED";
    default:
        return "UNKNOWN";
    }
}

static void dashCheckWifi()
{
    static unsigned long lastCheck = 0;
    if (wifiNetworkCount == 0)
        return;
    unsigned long now = millis();
    if (!staConnected && !staConnectAttemptActive && staRetryAt > 0 && (long)(now - staRetryAt) >= 0)
    {
        staRetryAt = 0;
        dashRotateAndConnect();
        return; // let the fresh attempt age from its own start timestamp
    }

    unsigned long checkInterval = staConnectAttemptActive ? 250UL : 1000UL;
    if (now - lastCheck < checkInterval)
        return;
    lastCheck = now;

    int wifiStatus = WiFi.status();
    bool connected = wifiStatus == WL_CONNECTED;
    bool timedOut = !connected && staConnectAttemptActive &&
                    now - staConnectStartedAt >= kDashStaConnectTimeoutMs;
    if (timedOut)
    {
        uint8_t reason = WiFi.lastDisconnectReason();
        const char *reasonName = WiFi.lastDisconnectReasonName();
        staConnectAttemptActive = false;
        WiFi.disconnect(false, false);
        dashGatewayOnStaDisconnected(WiFi.apNetif());
        if (staConsecutiveFailures < 255)
            staConsecutiveFailures++;
        const bool retryWithFullScan = staAttemptUsedSavedChannel;
        const unsigned long retryDelay = retryWithFullScan
                                             ? kDashStaDirectedRetryMs
                                             : kDashStaSavedPollMs;
        if (retryWithFullScan)
        {
            staForceFullScan = true;
            if (wifiActiveSlot >= 0 && wifiActiveSlot < static_cast<int8_t>(wifiNetworkCount))
                wifiNextRotateSlot = static_cast<uint8_t>(wifiActiveSlot);
        }
        staRetryAt = now + retryDelay;
        dashLog("[WIFI] STA connect timed out; status=" + String(dashWifiStatusName(wifiStatus)) +
                " reason=" + String(reasonName) + "(" + String(reason) + ")" +
                " retry " + String(retryWithFullScan ? "full scan" : "saved networks") +
                " in " + String(retryDelay / 1000) +
                "s, AP+STA stays up (fail#" + String(staConsecutiveFailures) + ")");
        connected = false;
    }

    if (connected != staConnected)
    {
        staConnected = connected;
        if (connected)
        {
            staConnectAttemptActive = false;
            staRetryAt = 0;
            staAttemptUsedSavedChannel = false;
            staForceFullScan = false;
            staConsecutiveFailures = 0; // reset diagnostics on successful connect
            dashLog("[WIFI] Connected to " + String(staSSID) + " IP: " + WiFi.localIP().toString());
            dashSyncApChannelToSta();
            dashGatewayOnStaConnected(WiFi.staNetif(), WiFi.apNetif());
            // Remember which slot just succeeded so the next reboot tries it
            // first. Avoids rotating through stale/dead networks on every boot.
            if (wifiActiveSlot >= 0 && wifiActiveSlot < (int8_t)wifiNetworkCount)
            {
                wifi_ap_record_t staInfo = {};
                prefs.begin(PREFS_NS, false);
                if (esp_wifi_sta_get_ap_info(&staInfo) == ESP_OK &&
                    staInfo.primary >= 1 && staInfo.primary <= 14 &&
                    wifiNetworks[wifiActiveSlot].channel != staInfo.primary)
                {
                    wifiNetworks[wifiActiveSlot].channel = staInfo.primary;
                    prefs.putUChar(dashWifiKey(static_cast<uint8_t>(wifiActiveSlot), "c").c_str(),
                                   staInfo.primary);
                }
                if ((int8_t)prefs.getUChar("wn_pref", 0xFF) != wifiActiveSlot)
                    prefs.putUChar("wn_pref", static_cast<uint8_t>(wifiActiveSlot));
                prefs.end();
            }
        }
        else
        {
            if (staConsecutiveFailures < 255)
                staConsecutiveFailures++;
            dashLog("[WIFI] Disconnected from " + String(staSSID) +
                    "; retry saved networks in " + String(kDashStaSavedPollMs / 1000) + "s (fail#" +
                    String(staConsecutiveFailures) + ")");
            dashGatewayOnStaDisconnected(WiFi.apNetif());
            staConnectAttemptActive = false;
            staRetryAt = now + kDashStaSavedPollMs;
        }
    }

}

// Cached scan results — a full-channel scan in APSTA mode briefly drops the
// AP beacon, so we do NOT scan more often than kDashScanMinIntervalMs even if
// the WebUI keeps polling. Cached JSON is returned for repeat calls inside the
// window, and a 429 with retry-after is returned if the cache is empty.
static String dashCachedScanJson;
static unsigned long dashLastScanAt = 0;
static constexpr unsigned long kDashScanMinIntervalMs = 30000;

static void handleWifiScan()
{
    unsigned long now = millis();
    bool force = server.hasArg("force") && server.arg("force") == "1";
    if (!force && dashLastScanAt != 0 && (now - dashLastScanAt) < kDashScanMinIntervalMs)
    {
        if (dashCachedScanJson.length() > 0)
        {
            server.send(200, "application/json", dashCachedScanJson);
        }
        else
        {
            unsigned long retryMs = kDashScanMinIntervalMs - (now - dashLastScanAt);
            server.sendHeader("Retry-After", String((retryMs + 999) / 1000).c_str());
            server.send(429, "application/json",
                        String("{\"ok\":false,\"error\":\"scan-throttled\",\"retry_ms\":") +
                            String(retryMs) + "}");
        }
        return;
    }
    // Delete any lingering async scan result before triggering a new blocking scan.
    // In APSTA mode a full-channel scan briefly pauses AP beacon delivery.
    // This is user-triggered only; we do NOT scan automatically on reconnect.
    WiFi.scanDelete();
    dashPrepareWifiScan();
    int n = WiFi.scanNetworks(false, false, false, 300);
    String j = "{\"networks\":[";
    for (int i = 0; i < n && i < 20; i++)
    {
        if (i)
            j += ",";
        j += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i).c_str()) + "\"";
        j += ",\"rssi\":" + String(WiFi.RSSI(i));
        wifi_auth_mode_t auth = WiFi.encryptionType(i);
        j += ",\"enc\":" + String(auth != WIFI_AUTH_OPEN ? "true" : "false");
        j += ",\"auth\":" + String(static_cast<int>(auth));
        j += ",\"ch\":" + String(WiFi.channel(i));
        j += "}";
    }
    j += "]}";
    WiFi.scanDelete();
    dashCachedScanJson = j;
    dashLastScanAt = millis();
    server.send(200, "application/json", j);
}

static void dashPersistWifiSlot(uint8_t slot)
{
    if (slot >= wifiNetworkCount)
        return;
    const DashWifiNetwork &n = wifiNetworks[slot];
    prefs.putString(dashWifiKey(slot, "s").c_str(), String(n.ssid));
    prefs.putString(dashWifiKey(slot, "p").c_str(), String(n.pass));
    prefs.putBool(dashWifiKey(slot, "t").c_str(), n.useStatic);
    prefs.putUChar(dashWifiKey(slot, "c").c_str(), n.channel);
    if (n.useStatic)
    {
        prefs.putString(dashWifiKey(slot, "i").c_str(), String(n.ip));
        prefs.putString(dashWifiKey(slot, "g").c_str(), String(n.gw));
        prefs.putString(dashWifiKey(slot, "m").c_str(), String(n.mask));
        prefs.putString(dashWifiKey(slot, "d").c_str(), String(n.dns));
    }
    else
    {
        prefs.remove(dashWifiKey(slot, "i").c_str());
        prefs.remove(dashWifiKey(slot, "g").c_str());
        prefs.remove(dashWifiKey(slot, "m").c_str());
        prefs.remove(dashWifiKey(slot, "d").c_str());
    }
}

static void dashRemoveWifiSlotKeys(uint8_t slot)
{
    prefs.remove(dashWifiKey(slot, "s").c_str());
    prefs.remove(dashWifiKey(slot, "p").c_str());
    prefs.remove(dashWifiKey(slot, "t").c_str());
    prefs.remove(dashWifiKey(slot, "i").c_str());
    prefs.remove(dashWifiKey(slot, "g").c_str());
    prefs.remove(dashWifiKey(slot, "m").c_str());
    prefs.remove(dashWifiKey(slot, "d").c_str());
    prefs.remove(dashWifiKey(slot, "c").c_str());
}

// Save to slot N (0..count). idx == count means append (new). Reconnect on save.
static void handleWifiConfig()
{
    if (!server.hasArg("ssid"))
    {
        server.send(200, "application/json", "{\"ok\":true}");
        return;
    }

    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (ssid.length() == 0 || ssid.length() > kDashMaxSsidLen || dashStaSsidLooksCorrupt(ssid))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid SSID or password\"}");
        return;
    }

    int idx = -1;
    if (server.hasArg("idx"))
        idx = server.arg("idx").toInt();
    if (idx < 0 || idx > wifiNetworkCount)
        idx = wifiNetworkCount; // append

    if (idx == kDashMaxWifiNetworks)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Max networks reached\"}");
        return;
    }

    String effectivePass = pass;
    if (idx >= 0 && idx < wifiNetworkCount && pass.length() == 0 && strlen(wifiNetworks[idx].pass) > 0)
        effectivePass = wifiNetworks[idx].pass;
    if (!dashStaConfigLengthValid(ssid, effectivePass))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid SSID or password\"}");
        return;
    }

    DashWifiNetwork &n = wifiNetworks[idx];
    dashClearWifiNetwork(n);
    strlcpy(n.ssid, ssid.c_str(), sizeof(n.ssid));
    strlcpy(n.pass, effectivePass.c_str(), sizeof(n.pass));
    n.useStatic = server.hasArg("static") && server.arg("static") == "1";
    if (n.useStatic)
    {
        strlcpy(n.ip, server.arg("ip").c_str(), sizeof(n.ip));
        strlcpy(n.gw, server.arg("gw").c_str(), sizeof(n.gw));
        strlcpy(n.mask, server.arg("mask").c_str(), sizeof(n.mask));
        strlcpy(n.dns, server.arg("dns").c_str(), sizeof(n.dns));
    }

    if (idx == wifiNetworkCount)
        wifiNetworkCount++;

    prefs.begin(PREFS_NS, false);
    prefs.putUChar("wn_cnt", wifiNetworkCount);
    dashPersistWifiSlot(idx);
    prefs.end();

    dashLog("[WIFI] Saved slot " + String(idx) + ": " + ssid);

    // Switch to newly saved slot and connect
    wifiNextRotateSlot = idx;
    dashApplyWifiSlot(idx);
    dashPrepareStaReconnect();

    server.send(200, "application/json", "{\"ok\":true,\"idx\":" + String(idx) + "}");
    dashScheduleSTAConnect(1000);
}

static void handleWifiConnect()
{
    if (!server.hasArg("idx"))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing idx\"}");
        return;
    }
    int idx = server.arg("idx").toInt();
    if (idx < 0 || idx >= wifiNetworkCount)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad idx\"}");
        return;
    }

    dashApplyWifiSlot(static_cast<uint8_t>(idx));
    if (strlen(apSSID) > 0 && strcmp(staSSID, apSSID) == 0)
    {
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"SSID matches AP hotspot\"}");
        return;
    }

    wifiNextRotateSlot = static_cast<uint8_t>(idx);
    dashPrepareStaReconnect();
    dashLog("[WIFI] Manual connect slot " + String(idx) + ": " + String(staSSID));

    server.send(200, "application/json",
                "{\"ok\":true,\"idx\":" + String(idx) +
                    ",\"ssid\":\"" + jsonEscape(staSSID) + "\"}");
    dashScheduleSTAConnect(100);
}

static void handleWifiDelete()
{
    if (!server.hasArg("idx"))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing idx\"}");
        return;
    }
    int idx = server.arg("idx").toInt();
    if (idx < 0 || idx >= wifiNetworkCount)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad idx\"}");
        return;
    }

    String removedSsid = wifiNetworks[idx].ssid;
    // Shift slots down
    for (uint8_t i = idx; i + 1 < wifiNetworkCount; i++)
        wifiNetworks[i] = wifiNetworks[i + 1];
    wifiNetworkCount--;
    dashClearWifiNetwork(wifiNetworks[wifiNetworkCount]);

    // Rewrite all slot keys
    prefs.begin(PREFS_NS, false);
    prefs.putUChar("wn_cnt", wifiNetworkCount);
    for (uint8_t i = 0; i < wifiNetworkCount; i++)
        dashPersistWifiSlot(i);
    for (uint8_t i = wifiNetworkCount; i < kDashMaxWifiNetworks; i++)
        dashRemoveWifiSlotKeys(i);
    // Adjust persisted preferred slot to stay within bounds after deletion.
    // If the deleted slot WAS the preferred one, reset to 0 so the next boot
    // doesn't try an empty/stale slot first.
    {
        uint8_t pref = prefs.getUChar("wn_pref", 0);
        if ((int)idx == (int)pref || pref >= wifiNetworkCount)
            prefs.putUChar("wn_pref", 0);
        else if (pref > idx)
            prefs.putUChar("wn_pref", pref - 1);
    }
    prefs.end();

    dashLog("[WIFI] Deleted slot " + String(idx) + ": " + removedSsid);

    // Adjust active slot if needed
    if (wifiActiveSlot == idx)
    {
        wifiActiveSlot = -1;
        if (staConnectAttemptActive || staConnected)
        {
            WiFi.disconnect(false, false);
            dashGatewayOnStaDisconnected(WiFi.apNetif());
            staConnectAttemptActive = false;
            staConnected = false;
        }
        if (wifiNetworkCount > 0)
        {
            wifiNextRotateSlot = 0;
            dashRotateAndConnect();
        }
        else
        {
            staSSID[0] = 0;
            staPass[0] = 0;
            // Keep AP+STA mode active even when no networks are saved.
            if (WiFi.getMode() != WIFI_AP_STA)
                WiFi.mode(WIFI_AP_STA);
        }
    }
    else if (wifiActiveSlot > idx)
    {
        wifiActiveSlot--;
    }
    if (wifiNextRotateSlot >= wifiNetworkCount)
        wifiNextRotateSlot = 0;

    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleWifiNetworks()
{
    String j = "{\"max\":";
    j += kDashMaxWifiNetworks;
    j += ",\"count\":";
    j += wifiNetworkCount;
    j += ",\"active\":";
    j += wifiActiveSlot;
    j += ",\"networks\":[";
    for (uint8_t i = 0; i < wifiNetworkCount; i++)
    {
        if (i)
            j += ",";
        const DashWifiNetwork &n = wifiNetworks[i];
        j += "{\"idx\":";
        j += i;
        j += ",\"ssid\":\"" + jsonEscape(n.ssid) + "\"";
        j += ",\"hasPass\":" + String(strlen(n.pass) > 0 ? "true" : "false");
        j += ",\"static\":" + String(n.useStatic ? "true" : "false");
        if (n.useStatic)
        {
            j += ",\"ip\":\"" + String(n.ip) + "\"";
            j += ",\"gw\":\"" + String(n.gw) + "\"";
            j += ",\"mask\":\"" + String(n.mask) + "\"";
            j += ",\"dns\":\"" + String(n.dns) + "\"";
        }
        j += "}";
    }
    j += "]}";
    server.send(200, "application/json", j);
}

static void handleWifiStatus()
{
    bool stored = wifiNetworkCount > 0;
    bool connectedNow = WiFi.status() == WL_CONNECTED;
    IPAddress staIp = WiFi.localIP();
    bool connected = connectedNow;
    String activeSsid = connectedNow ? WiFi.SSID() : String(staSSID);
    if (dashStaSsidLooksCorrupt(activeSsid))
        activeSsid = "";
    String j = "{\"connected\":";
    j += connected ? "true" : "false";
    j += ",\"ssid\":\"" + jsonEscape(activeSsid) + "\"";
    j += ",\"stored\":" + String(stored ? "true" : "false");
    j += ",\"count\":" + String(wifiNetworkCount);
    j += ",\"active\":" + String(wifiActiveSlot);
    int wifiStatus = WiFi.status();
    j += ",\"wifi_status\":" + String(wifiStatus);
    j += ",\"wifi_status_name\":\"";
    j += dashWifiStatusName(wifiStatus);
    j += "\"";
    j += ",\"disconnect_reason\":";
    j += String(static_cast<unsigned>(WiFi.lastDisconnectReason()));
    j += ",\"disconnect_reason_name\":\"";
    j += WiFi.lastDisconnectReasonName();
    j += "\"";
    if (staConnectAttemptActive)
        j += ",\"attempt_age_s\":" + String((millis() - staConnectStartedAt) / 1000);
    if (connected)
        j += ",\"ip\":\"" + staIp.toString() + "\"";
    j += ",\"static\":" + String(staStaticIP ? "true" : "false");
    if (staStaticIP)
    {
        j += ",\"cfg_ip\":\"" + staIP.toString() + "\"";
        j += ",\"cfg_gw\":\"" + staGW.toString() + "\"";
        j += ",\"cfg_mask\":\"" + staMask.toString() + "\"";
        j += ",\"cfg_dns\":\"" + staDNS.toString() + "\"";
    }
    if (!connected)
        j += ",\"connecting\":" + String(staConnectAttemptActive ? "true" : "false");
    j += ",\"fail_count\":" + String(staConsecutiveFailures);
    if (!connected && staRetryAt > 0)
    {
        unsigned long now = millis();
        long retryInMs = (long)(staRetryAt - now);
        j += ",\"retry_in_s\":" + String(retryInMs > 0 ? (retryInMs / 1000) : 0);
    }
    j += "}";
    server.send(200, "application/json", j);
}

// ── AP Config (hotspot name/password) ───────────────────────────

#ifdef ESP_PLATFORM
static const char *dashResetReasonName(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt_wdt";
    case ESP_RST_TASK_WDT:
        return "task_wdt";
    case ESP_RST_WDT:
        return "other_wdt";
    case ESP_RST_DEEPSLEEP:
        return "deepsleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    default:
        return "unknown";
    }
}

static bool dashReadTemperature(float &celsius)
{
    static temperature_sensor_handle_t tempHandle = nullptr;
    static bool tempReady = false;
    static bool tempTried = false;
    if (!tempTried)
    {
        tempTried = true;
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        if (temperature_sensor_install(&cfg, &tempHandle) == ESP_OK &&
            temperature_sensor_enable(tempHandle) == ESP_OK)
        {
            tempReady = true;
        }
    }
    return tempReady && temperature_sensor_get_celsius(tempHandle, &celsius) == ESP_OK;
}
#endif

#if defined(ESP_PLATFORM) && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static void dashReadCpuLoad(uint8_t &core0Load, uint8_t &core1Load, bool &valid)
{
    static uint32_t prevIdle[2] = {0, 0};
    static int64_t prevWallUs = 0;
    static bool havePrev = false;

    core0Load = 0;
    core1Load = 0;
    valid = false;

    constexpr UBaseType_t kMaxTasksForCpuStats = 48;
    TaskStatus_t tasks[kMaxTasksForCpuStats];
    configRUN_TIME_COUNTER_TYPE totalRunTime = 0;
    UBaseType_t count = uxTaskGetSystemState(tasks, kMaxTasksForCpuStats, &totalRunTime);
    if (count == 0)
        return;

    uint32_t idle[2] = {0, 0};
    bool seenIdle[2] = {false, false};
    for (UBaseType_t i = 0; i < count; i++)
    {
        const char *name = tasks[i].pcTaskName ? tasks[i].pcTaskName : "";
        if (strncmp(name, "IDLE", 4) != 0)
            continue;
        BaseType_t core = -1;
        size_t len = strlen(name);
        if (len > 0 && name[len - 1] >= '0' && name[len - 1] <= '1')
            core = name[len - 1] - '0';
        if (core >= 0 && core <= 1)
        {
            idle[core] = static_cast<uint32_t>(tasks[i].ulRunTimeCounter);
            seenIdle[core] = true;
        }
    }
    if (!seenIdle[0] || !seenIdle[1])
        return;

    int64_t nowUs = esp_timer_get_time();
    if (!havePrev)
    {
        prevIdle[0] = idle[0];
        prevIdle[1] = idle[1];
        prevWallUs = nowUs;
        havePrev = true;
        return;
    }

    uint32_t wallDelta = static_cast<uint32_t>(nowUs - prevWallUs);
    if (wallDelta < 100000)
        return;

    uint32_t idleDelta0 = idle[0] - prevIdle[0];
    uint32_t idleDelta1 = idle[1] - prevIdle[1];
    auto loadFromIdle = [](uint32_t idleDelta, uint32_t elapsedUs) -> uint8_t {
        uint32_t idlePct = elapsedUs ? (idleDelta * 100UL + elapsedUs / 2) / elapsedUs : 0;
        if (idlePct > 100)
            idlePct = 100;
        return static_cast<uint8_t>(100 - idlePct);
    };

    core0Load = loadFromIdle(idleDelta0, wallDelta);
    core1Load = loadFromIdle(idleDelta1, wallDelta);
    valid = true;
    prevIdle[0] = idle[0];
    prevIdle[1] = idle[1];
    prevWallUs = nowUs;
}
#else
static void dashReadCpuLoad(uint8_t &core0Load, uint8_t &core1Load, bool &valid)
{
    core0Load = 0;
    core1Load = 0;
    valid = false;
}
#endif

static void handleSystemStatus()
{
#ifdef ESP_PLATFORM
    dashMemoryDiagnostics.poll(dashBuildDiagnosticsRuntimeState());
    const DashDiagHeapSnapshot memory = dashMemoryDiagnostics.heapSnapshot();
    const DashDiagAllocationFailure allocationFailure = dashMemoryDiagnostics.allocationFailure();
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flashSize = 0;
    esp_flash_get_size(NULL, &flashSize);

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macText[18];
    snprintf(macText, sizeof(macText), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    wifi_ap_record_t apInfo;
    int rssi = 0;
    bool hasRssi = esp_wifi_sta_get_ap_info(&apInfo) == ESP_OK;
    if (hasRssi)
        rssi = apInfo.rssi;

    size_t spiffsTotal = 0, spiffsUsed = 0;
    bool spiffsOk = esp_spiffs_info(NULL, &spiffsTotal, &spiffsUsed) == ESP_OK;

    const esp_partition_t *running = esp_ota_get_running_partition();
    uint32_t appUsed = 0;
    if (running)
    {
        esp_partition_pos_t runningPos = {};
        runningPos.offset = running->address;
        runningPos.size = running->size;
        esp_image_metadata_t imageMeta = {};
        if (esp_image_get_metadata(&runningPos, &imageMeta) == ESP_OK)
            appUsed = imageMeta.image_len;
    }
    float tempC = 0.0f;
    bool hasTemp = dashReadTemperature(tempC);
    uint32_t cpuHz = static_cast<uint32_t>(esp_clk_cpu_freq());
    uint32_t cpuMhz = (cpuHz + 500000UL) / 1000000UL;
    uint32_t apbMhz = (static_cast<uint32_t>(esp_clk_apb_freq()) + 500000UL) / 1000000UL;
    uint32_t xtalMhz = (static_cast<uint32_t>(esp_clk_xtal_freq()) + 500000UL) / 1000000UL;
    bool pmDynamic = false;
    int pmMinMhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    int pmMaxMhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
#if CONFIG_PM_ENABLE
    esp_pm_config_t pmConfig;
    if (esp_pm_get_configuration(&pmConfig) == ESP_OK)
    {
        pmDynamic = pmConfig.min_freq_mhz != pmConfig.max_freq_mhz;
        pmMinMhz = pmConfig.min_freq_mhz;
        pmMaxMhz = pmConfig.max_freq_mhz;
    }
#endif
    wifi_mode_t wifiMode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&wifiMode);
    wifi_ps_type_t wifiPs = WIFI_PS_NONE;
    esp_wifi_get_ps(&wifiPs);
    const char *wifiModeText = "off";
    switch (wifiMode)
    {
    case WIFI_MODE_STA:
        wifiModeText = "STA";
        break;
    case WIFI_MODE_AP:
        wifiModeText = "AP";
        break;
    case WIFI_MODE_APSTA:
        wifiModeText = "AP+STA";
        break;
    default:
        wifiModeText = "off";
        break;
    }
    uint8_t cpu0Load = 0, cpu1Load = 0;
    bool hasCpuLoad = false;
    dashReadCpuLoad(cpu0Load, cpu1Load, hasCpuLoad);

    String j = "{\"chip\":\"ESP32-S3\"";
    j.reserve(3200);
    j += ",\"module\":\"ESP32-S3R8\"";
    j += ",\"target\":\"" CONFIG_IDF_TARGET "\"";
    j += ",\"cores\":" + String(chip.cores);
    j += ",\"revision\":" + String(chip.revision);
    j += ",\"cpu_mhz\":" + String(cpuMhz);
    j += ",\"cpu_default_mhz\":" + String(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    j += ",\"cpu_max_mhz\":240";
    j += ",\"cpu_core0_mhz\":" + String(cpuMhz);
    j += ",\"cpu_core1_mhz\":" + String(chip.cores > 1 ? cpuMhz : 0);
    j += ",\"cpu_policy\":\"" + String(pmDynamic ? "dynamic" : "fixed") + "\"";
    j += ",\"cpu_pm_min_mhz\":" + String(pmMinMhz);
    j += ",\"cpu_pm_max_mhz\":" + String(pmMaxMhz);
    j += ",\"cpu_overclock\":" + String(cpuMhz > 240 ? "true" : "false");
    j += ",\"cpu_load_valid\":" + String(hasCpuLoad ? "true" : "false");
    j += ",\"cpu0_load\":" + String(cpu0Load);
    j += ",\"cpu1_load\":" + String(cpu1Load);
    j += ",\"apb_mhz\":" + String(apbMhz);
    j += ",\"xtal_mhz\":" + String(xtalMhz);
    j += ",\"sram_bytes\":524288";
    j += ",\"rtc_sram_bytes\":16384";
    j += ",\"rom_bytes\":393216";
    j += ",\"idf\":\"" IDF_VER "\"";
    j += ",\"firmware\":\"" + jsonEscape(String(dashFirmwareVersion())) + "\"";
    j += ",\"git_sha\":\"" FIRMWARE_GIT_SHA "\"";
    j += ",\"ota_partition\":\"" + String(dashOtaPartitionName(running)) + "\"";
    j += ",\"ota_time\":\"" + jsonEscape(lastOtaUploadTime) + "\"";
    j += ",\"mac\":\"" + String(macText) + "\"";
    j += ",\"reset\":\"" + String(dashResetReasonName(esp_reset_reason())) + "\"";
    j += ",\"uptime\":" + String((millis() - startMs) / 1000);
    j += ",\"tasks\":" + String(uxTaskGetNumberOfTasks());
    j += ",\"core\":" + String(xPortGetCoreID());
    j += ",\"heap_total\":" + String(memory.combined.total);
    j += ",\"heap_free\":" + String(memory.combined.free);
    j += ",\"heap_min\":" + String(memory.combined.minimum);
    j += ",\"heap_largest\":" + String(memory.combined.largest);
    j += ",\"internal_total\":" + String(memory.internal.total);
    j += ",\"internal_free\":" + String(memory.internal.free);
    j += ",\"internal_min\":" + String(memory.internal.minimum);
    j += ",\"internal_largest\":" + String(memory.internal.largest);
    j += ",\"internal_allocated\":" + String(memory.internal.allocated);
    j += ",\"internal_allocated_blocks\":" + String(memory.internal.allocatedBlocks);
    j += ",\"internal_free_blocks\":" + String(memory.internal.freeBlocks);
    j += ",\"internal_total_blocks\":" + String(memory.internal.totalBlocks);
    j += ",\"internal_fragmentation\":" + String(DashMemoryDiagnostics::fragmentationPercent(memory.internal.free, memory.internal.largest));
    j += ",\"dma_total\":" + String(memory.dma.total);
    j += ",\"dma_free\":" + String(memory.dma.free);
    j += ",\"dma_min\":" + String(memory.dma.minimum);
    j += ",\"dma_largest\":" + String(memory.dma.largest);
    j += ",\"psram_total\":" + String(memory.psram.total);
    j += ",\"psram_free\":" + String(memory.psram.free);
    j += ",\"psram_min\":" + String(memory.psram.minimum);
    j += ",\"psram_largest\":" + String(memory.psram.largest);
    j += ",\"diag_recording\":" + String(dashMemoryDiagnostics.initialized() ? "true" : "false");
    j += ",\"diag_samples\":" + String(dashMemoryDiagnostics.sampleCount());
    j += ",\"diag_sample_capacity\":" + String(dashMemoryDiagnostics.sampleCapacity());
    j += ",\"diag_events\":" + String(dashMemoryDiagnostics.eventCount());
    j += ",\"diag_delta_10m\":" + String(dashMemoryDiagnostics.tenMinuteInternalDelta());
    j += ",\"diag_delta_boot\":" + String(dashMemoryDiagnostics.startupInternalDelta());
    j += ",\"diag_alloc_failures\":" + String(allocationFailure.sequence / 2U);
    j += ",\"diag_previous_boot\":" + String(dashMemoryDiagnostics.hasPreviousBoot() ? "true" : "false");
    j += ",\"flash_size\":" + String(flashSize);
    j += ",\"flash_speed\":" + String(80000000UL);
    j += ",\"app_addr\":" + String(running ? running->address : 0);
    j += ",\"app_size\":" + String(running ? running->size : 0);
    j += ",\"app_used\":" + String(appUsed);
    j += ",\"app_label\":\"" + String(running ? running->label : "") + "\"";
    j += ",\"spiffs_ok\":" + String(spiffsOk ? "true" : "false");
    j += ",\"spiffs_total\":" + String(spiffsTotal);
    j += ",\"spiffs_used\":" + String(spiffsUsed);
    j += ",\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
    j += ",\"wifi_mode\":\"" + String(wifiModeText) + "\"";
    j += ",\"wifi_sleep\":" + String(wifiPs == WIFI_PS_NONE ? "false" : "true");
    j += ",\"wifi_standard\":\"2.4GHz 802.11 b/g/n\"";
    j += ",\"wifi_max_mbps\":150";
    j += ",\"ble_supported\":" + String((chip.features & CHIP_FEATURE_BLE) ? "true" : "false");
#ifdef CONFIG_BT_ENABLED
    j += ",\"ble_enabled\":true";
    j += ",\"ble_status\":\"compiled\"";
#else
    j += ",\"ble_enabled\":false";
    j += ",\"ble_status\":\"firmware disabled\"";
#endif
    j += ",\"wifi_rssi\":";
    j += hasRssi ? String(rssi) : String("null");
    j += ",\"ap_clients\":" + String(WiFi.softAPgetStationNum());
    j += ",\"temp_c\":";
    if (hasTemp)
    {
        int tempX10 = (int)(tempC * 10.0f + (tempC >= 0 ? 0.5f : -0.5f));
        j += String(tempX10 / 10);
        j += ".";
        j += String(abs(tempX10 % 10));
    }
    else
    {
        j += "null";
    }
    j += "}";
    server.send(200, "application/json", j);
#else
    server.send(200, "application/json", "{\"chip\":\"native\",\"cores\":1}");
#endif
}

#ifdef ESP_PLATFORM
static constexpr char kDashDiagExportPath[] = "/wifi_nag_diag.tmp";
static constexpr char kDashDiagTasksPath[] = "/wifi_nag_tasks.tmp";

static bool dashDiagFilePrintf(File &file, const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(buffer))
        return false;
    return file.write(reinterpret_cast<const uint8_t *>(buffer),
                      static_cast<size_t>(length)) == static_cast<size_t>(length);
}

static bool dashDiagWriteJsonString(File &file, const char *text)
{
    if (!dashDiagFilePrintf(file, "\""))
        return false;
    const char *value = text ? text : "";
    for (size_t i = 0; value[i]; i++)
    {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        char escaped[7] = {};
        const char *out = escaped;
        size_t length = 0;
        if (c == '\"' || c == '\\')
        {
            escaped[0] = '\\';
            escaped[1] = static_cast<char>(c);
            length = 2;
        }
        else if (c == '\n' || c == '\r' || c == '\t')
        {
            escaped[0] = '\\';
            escaped[1] = c == '\n' ? 'n' : (c == '\r' ? 'r' : 't');
            length = 2;
        }
        else if (c < 0x20)
        {
            snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            length = 6;
        }
        else
        {
            escaped[0] = static_cast<char>(c);
            length = 1;
        }
        if (file.write(reinterpret_cast<const uint8_t *>(out), length) != length)
            return false;
    }
    return dashDiagFilePrintf(file, "\"");
}

static const char *dashDiagEventName(uint8_t code)
{
    switch (code)
    {
    case DASH_DIAG_EVENT_BOOT: return "boot";
    case DASH_DIAG_EVENT_WIFI_STATE: return "wifi_state";
    case DASH_DIAG_EVENT_AP_CLIENTS: return "ap_clients";
    case DASH_DIAG_EVENT_BLE_STATE: return "ble_state";
    case DASH_DIAG_EVENT_OTA_START: return "ota_start";
    case DASH_DIAG_EVENT_OTA_END: return "ota_end";
    case DASH_DIAG_EVENT_OTA_FAIL: return "ota_fail";
    case DASH_DIAG_EVENT_TASK_COUNT: return "task_count";
    case DASH_DIAG_EVENT_MEMORY_WARNING: return "memory_warning";
    case DASH_DIAG_EVENT_MEMORY_CRITICAL: return "memory_critical";
    case DASH_DIAG_EVENT_MEMORY_RECOVERED: return "memory_recovered";
    case DASH_DIAG_EVENT_ALLOC_FAILED: return "allocation_failed";
    case DASH_DIAG_EVENT_MANUAL_MARK: return "manual_mark";
    case DASH_DIAG_EVENT_RECORDS_CLEARED: return "records_cleared";
    case DASH_DIAG_EVENT_RESTART_REQUESTED: return "restart_requested";
    default: return "unknown";
    }
}

static const char *dashDiagTaskStateName(eTaskState state)
{
    switch (state)
    {
    case eRunning: return "running";
    case eReady: return "ready";
    case eBlocked: return "blocked";
    case eSuspended: return "suspended";
    case eDeleted: return "deleted";
    default: return "invalid";
    }
}

static bool dashDiagWriteTaskArray(File &file)
{
    UBaseType_t capacity = uxTaskGetNumberOfTasks() + 4;
    if (capacity > 64)
        capacity = 64;
    TaskStatus_t *tasks = static_cast<TaskStatus_t *>(
        heap_caps_calloc(capacity, sizeof(TaskStatus_t),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!tasks)
        return dashDiagFilePrintf(file, "[]");

    configRUN_TIME_COUNTER_TYPE totalRuntime = 0;
    const UBaseType_t count = uxTaskGetSystemState(tasks, capacity, &totalRuntime);
    bool ok = dashDiagFilePrintf(file, "[");
    for (UBaseType_t i = 0; ok && i < count; i++)
    {
        ok = dashDiagFilePrintf(file,
                                "%s{\"name\":",
                                i == 0 ? "" : ",");
        ok = ok && dashDiagWriteJsonString(file, tasks[i].pcTaskName);
        ok = ok && dashDiagFilePrintf(
                       file,
                       ",\"number\":%lu,\"state\":\"%s\",\"priority\":%lu,"
                       "\"base_priority\":%lu,\"stack_min_free_bytes\":%lu,\"runtime\":%llu}",
                       static_cast<unsigned long>(tasks[i].xTaskNumber),
                       dashDiagTaskStateName(tasks[i].eCurrentState),
                       static_cast<unsigned long>(tasks[i].uxCurrentPriority),
                       static_cast<unsigned long>(tasks[i].uxBasePriority),
                       static_cast<unsigned long>(tasks[i].usStackHighWaterMark),
                       static_cast<unsigned long long>(tasks[i].ulRunTimeCounter));
    }
    ok = ok && dashDiagFilePrintf(file, "]");
    heap_caps_free(tasks);
    return ok;
}

static bool dashWriteDiagnosticsExport(File &file)
{
    if (!file)
        return false;
    const DashDiagRuntimeState runtime = dashBuildDiagnosticsRuntimeState();
    dashMemoryDiagnostics.poll(runtime, true);
    const DashDiagHeapSnapshot heap = dashMemoryDiagnostics.heapSnapshot();
    const DashDiagAllocationFailure failure = dashMemoryDiagnostics.allocationFailure();
    const DashDiagPreviousBoot &previous = dashMemoryDiagnostics.previousBoot();

    bool ok = dashDiagFilePrintf(
        file,
        "{\"schema\":\"wifi-nag-diagnostics-v1\",\"meta\":{"
        "\"firmware\":\"%s\",\"git_sha\":\"%s\",\"idf\":\"%s\","
        "\"chip\":\"ESP32-S3R8\",\"reset\":\"%s\",\"uptime_ms\":%lu,"
        "\"sample_interval_ms\":%lu,\"sensitive_fields_removed\":true},",
        dashFirmwareVersion(), FIRMWARE_GIT_SHA, IDF_VER,
        dashResetReasonName(esp_reset_reason()),
        static_cast<unsigned long>(millis() - startMs),
        static_cast<unsigned long>(DashMemoryDiagnostics::kSampleIntervalMs));

    ok = ok && dashDiagFilePrintf(
                   file,
                   "\"summary\":{\"internal_total\":%lu,\"internal_free\":%lu,"
                   "\"internal_min\":%lu,\"internal_largest\":%lu,"
                   "\"internal_allocated\":%lu,\"internal_allocated_blocks\":%lu,"
                   "\"internal_free_blocks\":%lu,\"internal_total_blocks\":%lu,"
                   "\"internal_fragmentation\":%u,\"dma_total\":%lu,"
                   "\"dma_free\":%lu,\"dma_min\":%lu,\"dma_largest\":%lu,"
                   "\"psram_total\":%lu,\"psram_free\":%lu,\"psram_min\":%lu,"
                   "\"psram_largest\":%lu,\"delta_10m\":%ld,\"delta_boot\":%ld,"
                   "\"sample_count\":%lu,\"sample_capacity\":%lu,"
                   "\"event_count\":%lu,\"allocation_failure_count\":%lu},",
                   static_cast<unsigned long>(heap.internal.total),
                   static_cast<unsigned long>(heap.internal.free),
                   static_cast<unsigned long>(heap.internal.minimum),
                   static_cast<unsigned long>(heap.internal.largest),
                   static_cast<unsigned long>(heap.internal.allocated),
                   static_cast<unsigned long>(heap.internal.allocatedBlocks),
                   static_cast<unsigned long>(heap.internal.freeBlocks),
                   static_cast<unsigned long>(heap.internal.totalBlocks),
                   DashMemoryDiagnostics::fragmentationPercent(heap.internal.free, heap.internal.largest),
                   static_cast<unsigned long>(heap.dma.total),
                   static_cast<unsigned long>(heap.dma.free),
                   static_cast<unsigned long>(heap.dma.minimum),
                   static_cast<unsigned long>(heap.dma.largest),
                   static_cast<unsigned long>(heap.psram.total),
                   static_cast<unsigned long>(heap.psram.free),
                   static_cast<unsigned long>(heap.psram.minimum),
                   static_cast<unsigned long>(heap.psram.largest),
                   static_cast<long>(dashMemoryDiagnostics.tenMinuteInternalDelta()),
                   static_cast<long>(dashMemoryDiagnostics.startupInternalDelta()),
                   static_cast<unsigned long>(dashMemoryDiagnostics.sampleCount()),
                   static_cast<unsigned long>(dashMemoryDiagnostics.sampleCapacity()),
                   static_cast<unsigned long>(dashMemoryDiagnostics.eventCount()),
                   static_cast<unsigned long>(failure.sequence / 2U));

    ok = ok && dashDiagFilePrintf(
                   file,
                   "\"runtime\":{\"wifi_mode\":%u,\"wifi_status\":%u,"
                   "\"ap_clients\":%u,\"wifi_rssi\":%d,\"ble_protocol\":%u,"
                   "\"ble_flags\":%u,\"ble_rssi\":%d,\"ble_disconnect_reason\":%u,"
                   "\"ble_reconnect_count\":%lu,\"ble_disconnect_count\":%lu,"
                   "\"gateway_pending\":%u,\"gateway_pending_max\":%u,"
                   "\"gateway_pending_full\":%lu,\"gateway_timeouts\":%lu,"
                   "\"ota_running\":%s,\"can_online\":%s,\"can_write_enabled\":%s,"
                   "\"rx\":%lu,\"tx\":%lu,\"tx_error\":%lu},",
                   runtime.wifiMode, runtime.wifiStatus, runtime.apClients,
                   static_cast<int>(runtime.wifiRssi), runtime.bleProtocol,
                   runtime.bleFlags, static_cast<int>(runtime.bleRssi),
                   runtime.bleDisconnectReason,
                   static_cast<unsigned long>(runtime.bleReconnectCount),
                   static_cast<unsigned long>(runtime.bleDisconnectCount),
                   runtime.gatewayPending, runtime.gatewayPendingMax,
                   static_cast<unsigned long>(runtime.gatewayPendingFull),
                   static_cast<unsigned long>(runtime.gatewayTimeouts),
                   runtime.otaRunning ? "true" : "false",
                   runtime.canOnline ? "true" : "false",
                   runtime.canWriteEnabled ? "true" : "false",
                   static_cast<unsigned long>(runtime.rxCount),
                   static_cast<unsigned long>(runtime.txCount),
                   static_cast<unsigned long>(runtime.txErrorCount));

    ok = ok && dashDiagFilePrintf(file, "\"previous_boot\":");
    if (dashMemoryDiagnostics.hasPreviousBoot())
    {
        ok = ok && dashDiagFilePrintf(
                       file,
                       "{\"uptime_ms\":%lu,\"internal_free\":%lu,\"internal_min\":%lu,"
                       "\"internal_largest\":%lu,\"task_count\":%lu,"
                       "\"runtime_flags\":%lu,\"last_event\":\"%s\"},",
                       static_cast<unsigned long>(previous.uptimeMs),
                       static_cast<unsigned long>(previous.internalFree),
                       static_cast<unsigned long>(previous.internalMinimum),
                       static_cast<unsigned long>(previous.internalLargest),
                       static_cast<unsigned long>(previous.taskCount),
                       static_cast<unsigned long>(previous.runtimeFlags),
                       dashDiagEventName(static_cast<uint8_t>(previous.lastEvent)));
    }
    else
    {
        ok = ok && dashDiagFilePrintf(file, "null,");
    }

    ok = ok && dashDiagFilePrintf(file, "\"last_allocation_failure\":");
    if (failure.sequence != 0)
    {
        ok = ok && dashDiagFilePrintf(
                       file,
                       "{\"count\":%lu,\"uptime_ms\":%lu,\"requested_size\":%lu,"
                       "\"caps\":%lu,\"internal_free\":%lu,\"internal_largest\":%lu,"
                       "\"function\":",
                       static_cast<unsigned long>(failure.sequence / 2U),
                       static_cast<unsigned long>(failure.uptimeMs),
                       static_cast<unsigned long>(failure.requestedSize),
                       static_cast<unsigned long>(failure.caps),
                       static_cast<unsigned long>(failure.internalFree),
                       static_cast<unsigned long>(failure.internalLargest));
        ok = ok && dashDiagWriteJsonString(file, failure.functionName);
        ok = ok && dashDiagFilePrintf(file, "},");
    }
    else
    {
        ok = ok && dashDiagFilePrintf(file, "null,");
    }

    ok = ok && dashDiagFilePrintf(file, "\"tasks\":");
    ok = ok && dashDiagWriteTaskArray(file);
    ok = ok && dashDiagFilePrintf(file, ",\"timeline\":[");
    for (size_t i = 0; ok && i < dashMemoryDiagnostics.sampleCount(); i++)
    {
        const DashDiagSample *sample = dashMemoryDiagnostics.sampleAt(i);
        if (!sample)
            continue;
        ok = dashDiagFilePrintf(
            file,
            "%s{\"uptime_ms\":%lu,\"internal_free\":%lu,\"internal_min\":%lu,"
            "\"internal_largest\":%lu,\"internal_allocated\":%lu,"
            "\"internal_allocated_blocks\":%u,\"internal_free_blocks\":%u,"
            "\"dma_free\":%lu,\"dma_largest\":%lu,\"psram_free\":%lu,"
            "\"psram_largest\":%lu,\"tasks\":%u,\"wifi_mode\":%u,"
            "\"wifi_status\":%u,\"ap_clients\":%u,\"wifi_rssi\":%d,"
            "\"ble_protocol\":%u,\"ble_flags\":%u,\"ble_rssi\":%d,"
            "\"ble_disconnect_reason\":%u,\"ble_reconnect_count\":%lu,"
            "\"ble_disconnect_count\":%lu,\"gateway_pending\":%u,"
            "\"gateway_pending_max\":%u,\"gateway_pending_full\":%lu,"
            "\"gateway_timeouts\":%lu,\"runtime_flags\":%u,\"rx\":%lu,"
            "\"tx\":%lu,\"tx_error\":%lu}",
            i == 0 ? "" : ",",
            static_cast<unsigned long>(sample->uptimeMs),
            static_cast<unsigned long>(sample->internalFree),
            static_cast<unsigned long>(sample->internalMinimum),
            static_cast<unsigned long>(sample->internalLargest),
            static_cast<unsigned long>(sample->internalAllocated),
            sample->internalAllocatedBlocks, sample->internalFreeBlocks,
            static_cast<unsigned long>(sample->dmaFree),
            static_cast<unsigned long>(sample->dmaLargest),
            static_cast<unsigned long>(sample->psramFree),
            static_cast<unsigned long>(sample->psramLargest),
            sample->taskCount, sample->wifiMode, sample->wifiStatus,
            sample->apClients, static_cast<int>(sample->wifiRssi),
            sample->bleProtocol, sample->bleFlags, static_cast<int>(sample->bleRssi),
            sample->bleDisconnectReason,
            static_cast<unsigned long>(sample->bleReconnectCount),
            static_cast<unsigned long>(sample->bleDisconnectCount),
            sample->gatewayPending, sample->gatewayPendingMax,
            static_cast<unsigned long>(sample->gatewayPendingFull),
            static_cast<unsigned long>(sample->gatewayTimeouts),
            sample->runtimeFlags,
            static_cast<unsigned long>(sample->rxCount),
            static_cast<unsigned long>(sample->txCount),
            static_cast<unsigned long>(sample->txErrorCount));
    }

    ok = ok && dashDiagFilePrintf(file, "],\"events\":[");
    for (size_t i = 0; ok && i < dashMemoryDiagnostics.eventCount(); i++)
    {
        const DashDiagEvent *event = dashMemoryDiagnostics.eventAt(i);
        if (!event)
            continue;
        ok = dashDiagFilePrintf(
            file,
            "%s{\"uptime_ms\":%lu,\"type\":\"%s\",\"code\":%u,"
            "\"internal_free\":%lu,\"internal_largest\":%lu,"
            "\"delta_from_previous\":%ld,\"argument\":%lu,\"tasks\":%u,"
            "\"wifi_status\":%u,\"ap_clients\":%u,\"ble_protocol\":%u,"
            "\"runtime_flags\":%u}",
            i == 0 ? "" : ",",
            static_cast<unsigned long>(event->uptimeMs),
            dashDiagEventName(event->code), event->code,
            static_cast<unsigned long>(event->internalFree),
            static_cast<unsigned long>(event->internalLargest),
            static_cast<long>(event->deltaFromPrevious),
            static_cast<unsigned long>(event->argument), event->taskCount,
            event->wifiStatus, event->apClients, event->bleProtocol,
            event->runtimeFlags);
    }
    return ok && dashDiagFilePrintf(file, "]}");
}

static void handleDiagnosticsExport()
{
    File file = SPIFFS.open(kDashDiagExportPath, "w");
    if (!file || !dashWriteDiagnosticsExport(file))
    {
        file.close();
        SPIFFS.remove(kDashDiagExportPath);
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"diagnostics export failed\"}");
        return;
    }
    file.close();
    file = SPIFFS.open(kDashDiagExportPath, "r");
    if (!file)
    {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"diagnostics file unavailable\"}");
        return;
    }
    char filename[112];
    snprintf(filename, sizeof(filename),
             "attachment; filename=\"WiFi-NAG-DIAG-%s-%s-uptime%lu.json\"",
             dashFirmwareVersion(), FIRMWARE_GIT_SHA,
             static_cast<unsigned long>((millis() - startMs) / 1000U));
    server.sendHeader("Content-Disposition", filename);
    server.sendHeader("Cache-Control", "no-store");
    server.streamFile(file, "application/json");
    file.close();
    SPIFFS.remove(kDashDiagExportPath);
}

static void handleDiagnosticsTasks()
{
    File file = SPIFFS.open(kDashDiagTasksPath, "w");
    bool ok = file && dashDiagFilePrintf(file, "{\"ok\":true,\"tasks\":") &&
              dashDiagWriteTaskArray(file) && dashDiagFilePrintf(file, "}");
    file.close();
    if (!ok)
    {
        SPIFFS.remove(kDashDiagTasksPath);
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"task snapshot failed\"}");
        return;
    }
    file = SPIFFS.open(kDashDiagTasksPath, "r");
    server.sendHeader("Cache-Control", "no-store");
    server.streamFile(file, "application/json");
    file.close();
    SPIFFS.remove(kDashDiagTasksPath);
}

static void handleDiagnosticsMark()
{
    dashMemoryDiagnostics.recordEvent(DASH_DIAG_EVENT_MANUAL_MARK,
                                      dashBuildDiagnosticsRuntimeState());
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleDiagnosticsClear()
{
    dashMemoryDiagnostics.clear(dashBuildDiagnosticsRuntimeState());
    server.send(200, "application/json", "{\"ok\":true}");
}
#endif

#ifdef ESP_PLATFORM
static void dashSerialPrintHelp()
{
    Serial.println();
    Serial.println("ev-open-can-tools serial diagnostics");
    Serial.println("Commands:");
    Serial.println("  help           show this help");
    Serial.println("  system_status  print CPU/heap/WiFi summary");
    Serial.println("  can_status     print CAN/Nag summary");
    Serial.println();
}

static void dashSerialPrintSystemStatus()
{
    uint8_t cpu0Load = 0, cpu1Load = 0;
    bool hasCpuLoad = false;
    dashReadCpuLoad(cpu0Load, cpu1Load, hasCpuLoad);

    float tempC = 0.0f;
    bool hasTemp = dashReadTemperature(tempC);
    uint32_t cpuMhz = (static_cast<uint32_t>(esp_clk_cpu_freq()) + 500000UL) / 1000000UL;
    wifi_mode_t wifiMode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&wifiMode);

    const char *wifiModeText = "off";
    switch (wifiMode)
    {
    case WIFI_MODE_STA:
        wifiModeText = "STA";
        break;
    case WIFI_MODE_AP:
        wifiModeText = "AP";
        break;
    case WIFI_MODE_APSTA:
        wifiModeText = "AP+STA";
        break;
    default:
        break;
    }

    wifi_ap_record_t apInfo;
    bool hasRssi = esp_wifi_sta_get_ap_info(&apInfo) == ESP_OK;

    Serial.println();
    Serial.println("[system_status]");
    Serial.printf("uptime=%lus firmware=%s idf=%s\n", (millis() - startMs) / 1000, FIRMWARE_VERSION, IDF_VER);
    Serial.printf("cpu=%luMHz load=", (unsigned long)cpuMhz);
    if (hasCpuLoad)
        Serial.printf("CPU0 %u%% CPU1 %u%%\n", cpu0Load, cpu1Load);
    else
        Serial.println("sampling");
    Serial.printf("heap_free=%u heap_largest=%u heap_min=%u tasks=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                  (unsigned)uxTaskGetNumberOfTasks());
    Serial.printf("wifi=%s connected=%s ap_clients=%u",
                  wifiModeText,
                  WiFi.status() == WL_CONNECTED ? "yes" : "no",
                  (unsigned)WiFi.softAPgetStationNum());
    if (hasRssi)
        Serial.printf(" rssi=%d", apInfo.rssi);
    Serial.println();
    if (hasTemp)
        Serial.printf("temp=%.1fC\n", tempC);
    Serial.println();
}

static void dashSerialPrintCanStatus()
{
    unsigned long fpsX10 = static_cast<unsigned long>(fps * 10.0f + 0.5f);
    Serial.println();
    Serial.println("[can_status]");
    Serial.printf("can=%s can_write=%s nag_tx_active=%s hw=%u\n",
                  canOnline ? "online" : "offline",
                  canActive ? "ON" : "OFF",
                  dashInjectionActive() ? "ON" : "OFF",
                  (unsigned)hwMode);
    Serial.printf("rx=%lu tx=%lu txerr=%lu fps=%lu.%lu\n",
                  rxCount, txCount, txErrCount, fpsX10 / 10, fpsX10 % 10);
    Serial.println();
}

static void dashSerialRunCommand(char *cmd)
{
    char *start = cmd;
    while (*start == ' ' || *start == '\t')
        start++;
    char *end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        *--end = '\0';
    for (char *p = start; *p; p++)
    {
        if (*p >= 'A' && *p <= 'Z')
            *p = *p - 'A' + 'a';
    }

    if (strcmp(start, "help") == 0 || strcmp(start, "?") == 0)
        dashSerialPrintHelp();
    else if (strcmp(start, "system_status") == 0 || strcmp(start, "sys") == 0)
        dashSerialPrintSystemStatus();
    else if (strcmp(start, "can_status") == 0 || strcmp(start, "can") == 0)
        dashSerialPrintCanStatus();
    else if (*start)
        Serial.println("Unknown command. Type help.");
}

static void dashSerialDiagnosticsPoll()
{
    static char cmd[96];
    static uint8_t len = 0;
    static bool announced = false;

    if (!announced && millis() > 3000)
    {
        announced = true;
        Serial.println("[DIAG] Serial commands ready. Type help.");
    }

    int budget = 24;
    while (budget-- > 0 && Serial.available() > 0)
    {
        int ch = Serial.read();
        if (ch < 0)
            break;
        if (ch == '\r' || ch == '\n')
        {
            if (len > 0)
            {
                cmd[len] = '\0';
                dashSerialRunCommand(cmd);
                len = 0;
            }
            continue;
        }
        if (ch == 8 || ch == 127)
        {
            if (len > 0)
                len--;
            continue;
        }
        if (ch < 32 || ch > 126)
            continue;
        if (len < sizeof(cmd) - 1)
            cmd[len++] = static_cast<char>(ch);
    }
}
#else
static void dashSerialDiagnosticsPoll() {}
#endif

static void handleApConfig()
{
    String newPass = server.arg("pass");
    bool hasHidden = server.hasArg("hidden");
    bool newHidden = hasHidden && (server.arg("hidden") == "1" || server.arg("hidden") == "true");

    if (newPass.length() > 0 && !dashApPasswordLengthValid(newPass.length()))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Password must be 8-64 characters\"}");
        return;
    }

    strlcpy(apSSID, kDashFixedApSsid, sizeof(apSSID));
    if (newPass.length() > 0)
        strlcpy(apPass, newPass.c_str(), sizeof(apPass));
    if (hasHidden)
        apHidden = newHidden;

    prefs.begin(PREFS_NS, false);
    prefs.remove("ap_ssid");
    if (newPass.length() > 0)
        prefs.putString("ap_pass", newPass);
    if (hasHidden)
        prefs.putBool("ap_hidden", newHidden);
    prefs.end();

    dashLog("[WIFI] AP config updated: SSID=" + String(kDashFixedApSsid) + (apHidden ? " (hidden)" : "") +
            " channel=auto match STA");
    server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Saved. AP starts on CH1 and auto matches STA after WiFi connects.\"}");
}

static void handleApStatus()
{
    Preferences p;
    bool stored = false;
    if (p.begin(PREFS_NS, false))
    {
        stored = p.isKey("ap_pass") || p.isKey("ap_hidden");
        p.end();
    }
    String j = "{\"ssid\":\"" + jsonEscape(apSSID) + "\"";
    j += ",\"ip\":\"" + WiFi.softAPIP().toString() + "\"";
    j += ",\"clients\":" + String(WiFi.softAPgetStationNum());
    j += ",\"channel\":" + String(dashCurrentApChannel());
    j += ",\"channel_auto\":true";
    j += ",\"last_channel_sync_ms\":" + String(apLastChannelSyncMs);
    j += ",\"last_channel_sync_target\":" + String(apLastChannelSyncTarget);
    j += ",\"last_channel_sync_ok\":" + String(apLastChannelSyncOk ? "true" : "false");
    j += ",\"stored\":" + String(stored ? "true" : "false");
    j += ",\"hidden\":" + String(apHidden ? "true" : "false");
    j += "}";
    server.send(200, "application/json", j);
}

// Dashboard frame callback wrapper

static void webTask(void *)
{
    for (;;)
    {
        ArduinoOTA.handle();
        server.handleClient();
        if (dashRestartPending &&
            static_cast<int32_t>(millis() - dashRestartAtMs) >= 0)
        {
            dashRestartPending = false;
            appPrepareCanForRestart();
            vTaskDelay(pdMS_TO_TICKS(50));
            ESP.restart();
        }
        dashCheckWifi();
        dashMemoryDiagnostics.poll(dashBuildDiagnosticsRuntimeState());
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void mcpDashboardSetup(CarManagerBase *handler, CanDriver *driver)
{
    dashHandler = handler;
    dashDriver = driver;
    if (dashDriver)
        dashDriver->onSendFrame = mcpDashOnTxFrame;
    startMs = millis();
    fpsLastMs = millis();

    if (!SPIFFS.begin(true))
        dashLog("[WARN] SPIFFS mount failed");
    if (!dashMemoryDiagnostics.begin())
        dashLog("[WARN] Diagnostics timeline unavailable: PSRAM allocation failed");

    dashLoadPrefs();
    dashGatewayLoad();
    // Always boot in AP+STA mode so the STA interface is ready immediately.
    // This prevents connection failures to saved networks caused by late mode switching.
    dashStartAccessPoint(true);
    if (apHidden)
        dashLog("[WIFI] AP SSID is hidden");
    Serial.printf("[WIFI] AP: %s  IP: %s\n", apSSID, WiFi.softAPIP().toString().c_str());

    if (dashHandler)
    {
        dashHandler->onFrame = mcpDashOnFrame;
        appActiveHandler = dashHandler;
    }
    dashApplyRuntimeState();
    if (dashBootForcedNagOff)
        dashLog("[BOOT] OTA/firmware change forced NAG OFF");
    dashLog("[BOOT] WIFI-NAG mode: Nag killer + WiFi gateway");

    ArduinoOTA.setHostname("wifi-nag");
    ArduinoOTA.setPassword(kDashFixedOtaPassword);
    ArduinoOTA.onStart([]()
                       { dashLog("[OTA] Starting..."); });
    ArduinoOTA.onEnd([]()
                     { dashLog("[OTA] Done -- rebooting"); });
    ArduinoOTA.onError([](ota_error_t e)
                       { dashLog("[OTA] Error: " + String(e)); });
    ArduinoOTA.begin();

    server.on("/", HTTP_GET, handleRoot);
    server.on("/dashboard", HTTP_GET, handleLegacyDashboardRedirect);
    server.on("/legacy-dashboard", HTTP_GET, handleLegacyDashboardRedirect);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/config", HTTP_POST, handleConfig);
#if defined(NAG_KILLER)
    server.on("/api/config", HTTP_GET, handleNagApiConfig);
    server.on("/api/stats", HTTP_GET, handleNagApiStats);
    server.on("/api/mode", HTTP_POST, handleNagApiMode);
    server.on("/api/update", HTTP_POST, handleNagApiUpdate);
#endif
    server.on("/logging", HTTP_POST, handleLoggingConfig);
    server.on("/disable", HTTP_POST, handleDisable);
    server.on("/log", HTTP_GET, handleLog);
    server.on("/reboot", HTTP_POST, handleReboot);
    server.on("/update", HTTP_POST, handleOtaResult, handleOtaUpload);
    server.on("/ap_config", HTTP_POST, handleApConfig);
    server.on("/ap_status", HTTP_GET, handleApStatus);
    server.on("/wifi_scan", HTTP_GET, handleWifiScan);
    server.on("/wifi_config", HTTP_POST, handleWifiConfig);
    server.on("/wifi_status", HTTP_GET, handleWifiStatus);
    server.on("/system_status", HTTP_GET, handleSystemStatus);
    server.on("/diagnostics_export", HTTP_GET, handleDiagnosticsExport);
    server.on("/diagnostics_tasks", HTTP_GET, handleDiagnosticsTasks);
    server.on("/diagnostics_mark", HTTP_POST, handleDiagnosticsMark);
    server.on("/diagnostics_clear", HTTP_POST, handleDiagnosticsClear);
    server.on("/wifi_networks", HTTP_GET, handleWifiNetworks);
    server.on("/wifi_connect", HTTP_POST, handleWifiConnect);
    server.on("/wifi_delete", HTTP_POST, handleWifiDelete);
#if defined(ESP_PLATFORM) && defined(DASH_STA_AP_GATEWAY)
    server.on("/gateway_status", HTTP_GET, handleGatewayStatus);
    server.on("/gateway_dns", HTTP_GET, handleGatewayDnsGet);
    server.on("/gateway_dns", HTTP_POST, handleGatewayDnsPost);
    server.on("/gateway_dns_test", HTTP_GET, handleGatewayDnsTest);
    server.on("/gateway_dns_stats_reset", HTTP_POST, handleGatewayDnsStatsReset);
    server.on("/gateway_whitelist_add", HTTP_POST, handleGatewayWhitelistAdd);
    server.on("/gateway_blocked", HTTP_GET, handleGatewayBlocked);
    server.on("/gateway_blocked_clear", HTTP_POST, handleGatewayBlockedClear);
#endif

    server.begin();
    if (strlen(staSSID) > 0)
    {
        const bool hasSavedChannel = wifiActiveSlot >= 0 &&
                                     wifiActiveSlot < static_cast<int8_t>(wifiNetworkCount) &&
                                     wifiNetworks[wifiActiveSlot].channel >= 1 &&
                                     wifiNetworks[wifiActiveSlot].channel <= 14;
        dashScheduleSTAConnect(hasSavedChannel ? kDashStaBootDelayMs
                                               : kDashStaUnknownChannelBootDelayMs);
    }
#if CONFIG_FREERTOS_UNICORE
    xTaskCreate(webTask, "web", 8192, nullptr, 1, nullptr);
#else
#if defined(DASH_WIFI_PERF_TUNING)
    xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 0);
#else
    xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 1);
#endif
#endif
    Serial.println("[WEB] Dashboard: http://" + WiFi.softAPIP().toString());
    dashLog("[BOOT] WIFI-NAG ready");
}

static void mcpDashboardLoop()
{
    if (Update.isRunning())
        return;
    dashSerialDiagnosticsPoll();
    if (canOnline && millis() - lastFrameMs > 10000)
    {
        canOnline = false;
        dashLog("[CAN] Bus OFFLINE (timeout)");
    }
#if defined(DASH_RGB_STATUS_LED)
    appRefreshStatusLed(false);
#endif
}

#endif
