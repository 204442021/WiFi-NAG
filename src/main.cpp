/*
    PlatformIO entry point.
    Shared build settings live in platformio_profile.h.
    Logic is in the shared headers under include/.
*/

#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <Arduino.h>
#endif
#include "app.h"

#ifdef DRIVER_MCP2515
#include <SPI.h>
#include "drivers/mcp2515_driver.h"
#elif defined(DRIVER_ESP32_EXT_MCP2515)
#ifndef ESP_PLATFORM
#include <SPI.h>
#endif
#include "drivers/esp32_mcp2515_driver.h"
#elif defined(DRIVER_SAME51)
#include "drivers/same51_driver.h"
#elif defined(DRIVER_TWAI)
#include "drivers/twai_driver.h"
#ifndef ESP_PLATFORM
#include <Preferences.h>
#endif
#ifndef TWAI_TX_PIN
#define TWAI_TX_PIN GPIO_NUM_5
#endif
#ifndef TWAI_RX_PIN
#define TWAI_RX_PIN GPIO_NUM_4
#endif
#elif defined(DASH_CAN_DISABLED)
// WiFi-only product builds intentionally do not include or initialize a CAN driver.
#else
#error "Define DRIVER_MCP2515, DRIVER_ESP32_EXT_MCP2515, DRIVER_SAME51, or DRIVER_TWAI in build_flags"
#endif

#if defined(ESP_PLATFORM) && defined(DRIVER_TWAI) && !defined(DASH_CAN_DISABLED)
static bool appTwaiGpioReserved(gpio_num_t pin)
{
    int p = static_cast<int>(pin);
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (p >= 26 && p <= 32)
        return true; // embedded flash/PSRAM bus on ESP32-S3 modules
    if (p == 45 || p == 46)
        return true; // strapping/input-only pins
#elif defined(CONFIG_IDF_TARGET_ESP32)
#if !defined(DASH_ALLOW_CAN_GPIO_6_11) || !DASH_ALLOW_CAN_GPIO_6_11
    if (p >= 6 && p <= 11)
        return true; // SPI flash pins on common ESP32 modules
#endif
#endif
    return false;
}

static bool appTwaiGpioValid(gpio_num_t pin, bool tx)
{
    int p = static_cast<int>(pin);
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    constexpr int kMaxGpio = 48;
#else
    constexpr int kMaxGpio = 39;
#endif
    if (p < 0 || p > kMaxGpio || appTwaiGpioReserved(pin))
        return false;
#if defined(CONFIG_IDF_TARGET_ESP32)
    if (tx && p >= 34 && p <= 39)
        return false; // input-only pins cannot drive TWAI TX
#else
    (void)tx;
#endif
    return true;
}
#endif

static void app_main_setup()
{
#if defined(PRODUCT_WIFI_MAX) && defined(ESP32_DASHBOARD)
    delay(1500);
    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 1000)
    {
    }
#ifndef NATIVE_BUILD
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);
#endif
    mcpDashboardSetup(nullptr, nullptr);
#elif defined(DRIVER_MCP2515)
    appSetup<MCP2515Driver>(std::make_unique<MCP2515Driver>(PIN_CAN_CS), "MCP25625 ready @ 500k");
#ifdef ESP32_DASHBOARD
    mcpDashboardSetup(appHandler.get(), appDriver.get());
#endif
#elif defined(DRIVER_ESP32_EXT_MCP2515)
#ifndef ESP_PLATFORM
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, PIN_CAN_CS);
    SPI.setFrequency(8000000);
#endif
    auto drv = std::make_unique<ESP32_MCP2515Driver>(PIN_CAN_CS);
    MCP2515 *mcpPtr = &drv->mcp();
    appSetup<ESP32_MCP2515Driver>(std::move(drv), "ESP32 + MCP2515 ready @ 500k");
#ifdef ESP32_DASHBOARD
    mcpDashboardSetup(appHandler.get(), appDriver.get(), mcpPtr);
#endif
#elif defined(DRIVER_SAME51)
    appSetup<SAME51Driver>(std::make_unique<SAME51Driver>(), "SAME51 CAN ready @ 500k");
#elif defined(DRIVER_TWAI)
    // Load TWAI pins from NVS (survives OTA); fall back to compile-time defaults
    gpio_num_t twaiTx = TWAI_TX_PIN;
    gpio_num_t twaiRx = TWAI_RX_PIN;
    {
        Preferences canPrefs;
        if (canPrefs.begin("can", false))
        {
            int8_t tx = canPrefs.getChar("tx", -1);
            int8_t rx = canPrefs.getChar("rx", -1);
            canPrefs.end();
            if (appTwaiGpioValid((gpio_num_t)tx, true) && appTwaiGpioValid((gpio_num_t)rx, false) && tx != rx)
            {
                twaiTx = (gpio_num_t)tx;
                twaiRx = (gpio_num_t)rx;
            }
        }
    }
    appSetup<TWAIDriver>(std::make_unique<TWAIDriver>(twaiTx, twaiRx), "ESP32 TWAI ready @ 500k");
#ifdef ESP32_DASHBOARD
    mcpDashboardSetup(appHandler.get(), appDriver.get());
#endif
#endif
}

static bool app_main_loop()
{
#if defined(PRODUCT_WIFI_MAX) && defined(ESP32_DASHBOARD)
    mcpDashboardLoop();
    return false;
#elif defined(DRIVER_MCP2515)
    bool processed = appLoop<MCP2515Driver>();
#ifdef ESP32_DASHBOARD
    mcpDashboardLoop();
#endif
    return processed;
#elif defined(DRIVER_ESP32_EXT_MCP2515)
    bool processed = appLoop<ESP32_MCP2515Driver>();
#ifdef ESP32_DASHBOARD
    mcpDashboardLoop();
#endif
    return processed;
#elif defined(DRIVER_SAME51)
    return appLoop<SAME51Driver>();
#elif defined(DRIVER_TWAI)
    bool processed = appLoop<TWAIDriver>();
#ifdef ESP32_DASHBOARD
    mcpDashboardLoop();
#endif
    return processed;
#endif
}

#if defined(ESP_PLATFORM) && defined(DRIVER_TWAI) && !defined(DASH_CAN_DISABLED)
#ifndef APP_CAN_TASK_STACK
#define APP_CAN_TASK_STACK 6144
#endif
#ifndef APP_CAN_TASK_PRIORITY
#define APP_CAN_TASK_PRIORITY 18
#endif
#ifndef APP_CAN_TASK_CORE
#define APP_CAN_TASK_CORE 0
#endif

static void app_can_task(void *)
{
    appCanTaskDedicated = true;
    for (;;)
    {
        bool processed = appLoop<TWAIDriver>();
        appCanTaskLoops = appCanTaskLoops + 1;
        if (!processed)
        {
            appCanTaskIdleLoops = appCanTaskIdleLoops + 1;
            vTaskDelay(1);
        }
    }
}

static bool app_start_can_task()
{
    TaskHandle_t task = nullptr;
#if CONFIG_FREERTOS_UNICORE
    BaseType_t ok = xTaskCreate(app_can_task, "can_rt", APP_CAN_TASK_STACK, nullptr,
                                APP_CAN_TASK_PRIORITY, &task);
#else
    BaseType_t ok = xTaskCreatePinnedToCore(app_can_task, "can_rt", APP_CAN_TASK_STACK, nullptr,
                                            APP_CAN_TASK_PRIORITY, &task, APP_CAN_TASK_CORE);
#endif
    appCanTaskDedicated = ok == pdPASS;
    return ok == pdPASS;
}
#endif

#ifdef ESP_PLATFORM
extern "C" void app_main(void)
{
    esp_err_t nvsErr = nvs_flash_init();
    if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvsErr = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvsErr);

    app_main_setup();
#if defined(DRIVER_TWAI) && !defined(PRODUCT_WIFI_MAX) && !defined(DASH_CAN_DISABLED)
    bool canTaskStarted = app_start_can_task();
    while (true)
    {
        if (!canTaskStarted)
        {
            if (!app_main_loop())
                vTaskDelay(1);
            continue;
        }
#ifdef ESP32_DASHBOARD
        mcpDashboardLoop();
#endif
        vTaskDelay(1);
    }
#else
    while (true)
    {
        if (!app_main_loop())
            vTaskDelay(1);
    }
#endif
}
#else
void setup()
{
    app_main_setup();
}

void loop()
{
    app_main_loop();
}
#endif
