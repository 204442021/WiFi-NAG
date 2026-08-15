#pragma once

#include <memory>
#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "can_helpers.h"
#include "handlers.h"
#include "wifi_nag_can_ids.h"

#if defined(ESP_PLATFORM) && defined(BLE_BRIDGE)
#include "ble/brake_state.h"
#include "ble/bridge_client.h"
#include "obstacle_shift_controller.h"
#endif

#ifndef NATIVE_BUILD
#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h"
#else
#include <Arduino.h>
#endif
#endif
#if defined(DASH_RGB_STATUS_LED) && !defined(NATIVE_BUILD) && !defined(ESP_PLATFORM)
#include <esp32-hal-rgb-led.h>
#endif
#if defined(DASH_RGB_STATUS_LED) && defined(ESP_PLATFORM)
#include <led_strip.h>
#endif

#ifndef PIN_LED
#define PIN_LED 2
#endif

using SelectedHandler = NagHandler;

static std::unique_ptr<CanDriver> appDriver;
static std::unique_ptr<CarManagerBase> appHandler;
static CarManagerBase *appActiveHandler = nullptr;

#if defined(ESP_PLATFORM) && defined(DRIVER_TWAI)
static volatile bool appCanTaskDedicated = false;
static volatile uint32_t appCanTaskLoops = 0;
static volatile uint32_t appCanTaskIdleLoops = 0;
#endif

static volatile bool frameReady = true;
static void canISR() { frameReady = true; }

static volatile bool appCanRestartPreparing = false;
static volatile bool appCanOtaPreparing = false;
static bool appCanWriteModeKnown = false;
static bool appLastWriteEnabled = false;
static uint8_t appStableNagFrameCount = 0;
static uint32_t appLastStableNagFrameMs = 0;

static bool appPrepareCanForOta();
static void appResumeCanAfterOtaFailure();
static bool appPrepareCanForRestart();
static bool appCanTransmitRuntimeReady();

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_RGB_STATUS_LED)
static void appRefreshStatusLed(bool force = false);
static void appWriteStatusLed(uint8_t red, uint8_t green, uint8_t blue);
#endif
#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_INJECTION_TOGGLE_PIN)
static void appPollInjectionToggleButton();
#endif

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)
#include "web/mcp2515_dashboard.h"
#endif

static bool appSyncCanWriteMode(bool desiredWriteEnabled)
{
    if (!appDriver)
        return false;
    if (appCanRestartPreparing)
        desiredWriteEnabled = false;
    if (appCanWriteModeKnown && appLastWriteEnabled == desiredWriteEnabled)
        return true;

    appDriver->setTransmitGate(false);
    appStableNagFrameCount = 0;
    appLastStableNagFrameMs = 0;
    const bool ok = appDriver->setWriteEnabled(desiredWriteEnabled);
    if (ok)
    {
        appCanWriteModeKnown = true;
        appLastWriteEnabled = desiredWriteEnabled;
    }
    return ok;
}

static void appRefreshNagRuntimeGate()
{
#if defined(NAG_KILLER)
    nagKillerRuntime = nagKillerEnabled && canActive &&
                       appCanTransmitRuntimeReady() &&
                       !Update.isRunning() && !appCanOtaPreparing &&
                       !appCanRestartPreparing;
#else
    nagKillerRuntime = false;
#endif
}

static bool appCanTransmitRuntimeReady()
{
    return appDriver && appCanWriteModeKnown && appLastWriteEnabled &&
           appDriver->transmitReady() && !appCanOtaPreparing &&
           !appCanRestartPreparing;
}

static void appResetCanStability()
{
    appStableNagFrameCount = 0;
    appLastStableNagFrameMs = 0;
    if (appDriver)
        appDriver->setTransmitGate(false);
    appRefreshNagRuntimeGate();
}

static bool appNagFrameChecksumValid(const CanFrame &frame)
{
    if (frame.id != 0x370 || frame.dlc != 8)
        return false;
    uint16_t sum = 0x73;
    for (uint8_t index = 0; index < 7; ++index)
        sum = static_cast<uint16_t>(sum + frame.data[index]);
    return static_cast<uint8_t>(sum & 0xFFU) == frame.data[7];
}

static void appObserveCanStability(const CanFrame &frame)
{
    if (frame.id != 0x370)
        return;

    const uint32_t now = millis();
    const bool checksumValid = appNagFrameChecksumValid(frame);
    if (!checksumValid ||
        (appLastStableNagFrameMs != 0 &&
         now - appLastStableNagFrameMs > 100U))
    {
        appStableNagFrameCount = 0;
    }

    if (!checksumValid)
    {
        appLastStableNagFrameMs = 0;
        appRefreshNagRuntimeGate();
        return;
    }

    appLastStableNagFrameMs = now;
    if (appStableNagFrameCount < 3)
        appStableNagFrameCount++;

    if (appStableNagFrameCount >= 3 && canActive &&
        appCanWriteModeKnown && appLastWriteEnabled &&
        !Update.isRunning() && !appCanOtaPreparing &&
        !appCanRestartPreparing)
        appDriver->setTransmitGate(true);

    appRefreshNagRuntimeGate();
}

static bool appPrepareCanForOta()
{
    if (appCanRestartPreparing)
        return false;
    appCanOtaPreparing = true;
    nagKillerRuntime = false;
    // OTA reception must never depend on the hardware TX queue becoming
    // idle. Closing the software gate is synchronous and prevents any new
    // frame from entering the queue; final draining belongs to the deferred
    // restart path after the HTTP response has completed.
    if (appDriver)
        appDriver->setTransmitGate(false);
    return true;
}

static void appResumeCanAfterOtaFailure()
{
    if (appCanRestartPreparing)
        return;
    if (appDriver)
        appDriver->clearReceiveQueue();
    appCanOtaPreparing = false;
    appResetCanStability();
}

static bool appPrepareCanForRestart()
{
    appCanRestartPreparing = true;
    appCanOtaPreparing = false;
    nagKillerRuntime = false;
    appCanWriteModeKnown = true;
    appLastWriteEnabled = false;
#if defined(ESP_PLATFORM) && defined(BLE_BRIDGE)
    obstacleShiftController.reset();
    brakeStateMailbox.endSession();
#endif
    bool idle = false;
    if (appDriver)
    {
        idle = appDriver->quiesceTransmit(100);
        appDriver->prepareForRestart();
    }
    return idle;
}

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_RGB_STATUS_LED)
static void appWriteStatusLed(uint8_t red, uint8_t green, uint8_t blue)
{
#ifdef ESP_PLATFORM
    static led_strip_handle_t strip = nullptr;
    if (!strip)
    {
        led_strip_config_t scfg = {};
        scfg.strip_gpio_num = PIN_LED;
        scfg.max_leds = 1;
        scfg.led_model = LED_MODEL_WS2812;
        scfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
        led_strip_rmt_config_t rcfg = {};
        rcfg.resolution_hz = 10 * 1000 * 1000;
        if (led_strip_new_rmt_device(&scfg, &rcfg, &strip) != ESP_OK)
            return;
    }
    led_strip_set_pixel(strip, 0, red, green, blue);
    led_strip_refresh(strip);
#elif defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    rgbLedWrite(PIN_LED, red, green, blue);
#else
    neopixelWrite(PIN_LED, red, green, blue);
#endif
}

static void appRefreshStatusLed(bool force)
{
    static bool known = false;
    static bool lastInjecting = false;
    static bool lastConnected = false;
    static bool lastOta = false;
    static uint8_t lastLevel = 0;
    static bool lastEmittedOn = true;

    bool injecting = canActive;
    bool connected = (WiFi.softAPgetStationNum() > 0) || (WiFi.status() == WL_CONNECTED);
    bool ota = Update.isRunning();
    uint8_t level = dashLedBrightness;

    // Solid when connected (or OTA), 1 Hz blink otherwise.
    bool blinkOn = (millis() % 1000UL) < 500UL;
    bool emittedOn = (connected || ota) ? true : blinkOn;

    if (!force && known
        && lastInjecting == injecting
        && lastConnected == connected
        && lastOta == ota
        && lastLevel == level
        && lastEmittedOn == emittedOn)
        return;

    uint8_t r = 0, g = 0, b = 0;
    if (emittedOn)
    {
        if (ota)
            b = level;          // OTA: solid blue
        else if (injecting)
            g = level;
        else
            r = level;
    }
    appWriteStatusLed(r, g, b);

    lastInjecting = injecting;
    lastConnected = connected;
    lastOta = ota;
    lastLevel = level;
    lastEmittedOn = emittedOn;
    known = true;
}
#endif

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_INJECTION_TOGGLE_PIN)
static void appPollInjectionToggleButton()
{
    static bool rawState = HIGH;
    static bool stableState = HIGH;
    static unsigned long lastChangeMs = 0;

    bool sample = digitalRead(DASH_INJECTION_TOGGLE_PIN);
    unsigned long now = millis();

    if (sample != rawState)
    {
        rawState = sample;
        lastChangeMs = now;
    }

    if ((now - lastChangeMs) < 35 || sample == stableState)
        return;

    stableState = sample;
    if (stableState == LOW)
        dashToggleCanActive("GPIO41");
}
#endif

template <typename Driver>
static void appSetup(std::unique_ptr<Driver> drv, const char *readyMsg,
                     bool initialWriteEnabled = false)
{
    appHandler = std::make_unique<SelectedHandler>();
    appActiveHandler = appHandler.get();
    delay(1500);
    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 1000)
    {
    }

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_INJECTION_TOGGLE_PIN)
    pinMode(DASH_INJECTION_TOGGLE_PIN, INPUT_PULLUP);
#endif

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_RGB_STATUS_LED)
    appRefreshStatusLed(true);
#else
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);
#endif

    appDriver = std::move(drv);
    // Configure the acceptance filter before the first TWAI installation so
    // startup never needs an immediate stop/uninstall/reinstall cycle.
    appDriver->setFilters(kWifiNagObservedIds, kWifiNagObservedIdCount);
    const bool initialized = appDriver->init();
    if (!initialized)
    {
        Serial.println("CAN init failed");
    }
    appCanWriteModeKnown = initialized;
    appLastWriteEnabled = initialized && initialWriteEnabled;
    appResetCanStability();

    if constexpr (Driver::kSupportsISR)
    {
        appDriver->enableInterrupt(canISR);
    }

    Serial.println(readyMsg);

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)
    delay(2000);
#endif
}

template <typename Driver>
static bool appLoop()
{
#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_RGB_STATUS_LED)
    appRefreshStatusLed(false);
#endif
#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)
    if (!Update.isRunning() && !appCanOtaPreparing &&
        !appCanRestartPreparing)
        appSyncCanWriteMode(canActive);
    appRefreshNagRuntimeGate();
#if defined(ESP_PLATFORM) && defined(BLE_BRIDGE)
    BrakeStateView brakeView;
    if (!brakeStateMailbox.read(brakeView))
        brakeView = BrakeStateView{};
    ObstacleShiftRuntimeInputs shiftRuntime{
        static_cast<bool>(obstacleShiftFeatureEnabled),
        appCanTransmitRuntimeReady() && canOnline,
    };
    const uint32_t shiftNowMs = millis();
    obstacleShiftController.tick(brakeView, shiftRuntime, shiftNowMs);
#endif
    if (Update.isRunning())
    {
        delay(1);
        return false;
    }

#if defined(DASH_INJECTION_TOGGLE_PIN)
    appPollInjectionToggleButton();
#endif
#else
    appSyncCanWriteMode(true);
#endif

    if constexpr (Driver::kSupportsISR)
    {
        if (!frameReady)
            return false;
        frameReady = false;
    }

    CanFrame frame;
    CarManagerBase *h = appActiveHandler ? appActiveHandler : appHandler.get();
    uint8_t framesThisLoop = 0;
    bool processedFrame = false;
    for (;;)
    {
#if defined(ESP_PLATFORM) && defined(BLE_BRIDGE)
        BrakeStateView brakeBeforeRead;
        if (!brakeStateMailbox.read(brakeBeforeRead))
            brakeBeforeRead = BrakeStateView{};
        shiftRuntime.featureEnabled =
            static_cast<bool>(obstacleShiftFeatureEnabled);
        shiftRuntime.canWriteReady = appCanTransmitRuntimeReady() && canOnline;
        const uint32_t beforeReadMs = millis();
        obstacleShiftController.tick(brakeBeforeRead,
                                     shiftRuntime,
                                     beforeReadMs);
#endif
        if (!appDriver->read(frame))
            break;
        processedFrame = true;
        if (frame.bus == CAN_BUS_ANY)
            frame.bus = CAN_BUS_DEFAULT;
#if defined(ESP_PLATFORM) && defined(BLE_BRIDGE)
        BrakeStateView brakeAfterRead;
        if (!brakeStateMailbox.read(brakeAfterRead))
            brakeAfterRead = BrakeStateView{};
        shiftRuntime.featureEnabled =
            static_cast<bool>(obstacleShiftFeatureEnabled);
        shiftRuntime.canWriteReady = appCanTransmitRuntimeReady() && canOnline;
        const uint32_t afterReadMs = millis();
        obstacleShiftController.observeFrame(frame,
                                             brakeAfterRead,
                                             shiftRuntime,
                                             afterReadMs,
                                             *appDriver);
#endif
        appObserveCanStability(frame);
#if !(defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_RGB_STATUS_LED))
        digitalWrite(PIN_LED, LOW);
#endif
        h->frameCount++;
#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)
        CanFrame original = frame;
        h->handleMessage(frame, *appDriver);
        dashPostProcessFrame(original, *appDriver);
#else
        h->handleMessage(frame, *appDriver);
#endif
#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)
        if (++framesThisLoop >= 32)
        {
            yield();
            break;
        }
#endif
    }
#if !(defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_RGB_STATUS_LED))
    digitalWrite(PIN_LED, HIGH);
#endif
    return processedFrame;
}
