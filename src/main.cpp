/*
    WIFI-NAG firmware entry point.
    This product targets ESP32-S3 native TWAI CAN with the WebUI/DNS gateway.
*/

#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <Arduino.h>
#endif

#include "app.h"
#include "drivers/twai_driver.h"

#ifndef TWAI_TX_PIN
#define TWAI_TX_PIN GPIO_NUM_15
#endif
#ifndef TWAI_RX_PIN
#define TWAI_RX_PIN GPIO_NUM_16
#endif

#if defined(ESP_PLATFORM)
static constexpr uint32_t APP_OTA_HEALTH_WINDOW_MS = 15000UL;
static bool appOtaHealthObserving = false;
static bool appOtaHealthDecided = false;
static uint32_t appOtaHealthDeadlineMs = 0;

static void appStartOtaHealthObservation()
{
    if (appOtaHealthDecided || appOtaHealthObserving)
        return;

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (!running ||
        esp_ota_get_state_partition(running, &state) != ESP_OK ||
        (state != ESP_OTA_IMG_NEW &&
         state != ESP_OTA_IMG_PENDING_VERIFY))
    {
        appOtaHealthDecided = true;
        return;
    }

    appOtaHealthObserving = true;
    appOtaHealthDeadlineMs = millis() + APP_OTA_HEALTH_WINDOW_MS;
    Serial.println("[OTA] Pending image health observation started");
}

static void appServiceOtaHealthObservation()
{
    if (!appOtaHealthObserving ||
        static_cast<int32_t>(millis() - appOtaHealthDeadlineMs) < 0)
        return;

    appOtaHealthObserving = false;
    appOtaHealthDecided = true;
    CanDriverDiagnostics diagnostics = {};
    const bool canHealthy =
        appDriver &&
        appDriver->getDiagnostics(diagnostics) &&
        diagnostics.available &&
        diagnostics.state == CanDriverState::Running &&
        !diagnostics.safetyTripped;
    if (!canHealthy)
    {
        Serial.println("[OTA] Pending image failed health observation; rollback");
        (void)esp_ota_mark_app_invalid_rollback_and_reboot();
        ESP.restart();
        return;
    }

    const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    if (result != ESP_OK)
    {
        Serial.printf("[OTA] Mark valid failed=%s; rollback\n",
                      esp_err_to_name(result));
        (void)esp_ota_mark_app_invalid_rollback_and_reboot();
        ESP.restart();
        return;
    }
    Serial.println("[OTA] Pending image marked valid");
}

static bool appTwaiGpioReserved(gpio_num_t pin)
{
    int p = static_cast<int>(pin);
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (p >= 26 && p <= 32)
        return true; // embedded flash/PSRAM bus on ESP32-S3 modules
    if (p == 45 || p == 46)
        return true; // strapping/input-only pins
#endif
    return false;
}

static bool appTwaiGpioValid(gpio_num_t pin, bool tx)
{
    (void)tx;
    int p = static_cast<int>(pin);
    return p >= 0 && p <= 48 && !appTwaiGpioReserved(pin);
}
#endif

static void app_main_setup()
{
    gpio_num_t twaiTx = TWAI_TX_PIN;
    gpio_num_t twaiRx = TWAI_RX_PIN;

#if defined(ESP_PLATFORM)
    Preferences canPrefs;
    if (canPrefs.begin("can", false))
    {
        int8_t tx = canPrefs.getChar("tx", -1);
        int8_t rx = canPrefs.getChar("rx", -1);
        canPrefs.end();
        if (appTwaiGpioValid((gpio_num_t)tx, true) &&
            appTwaiGpioValid((gpio_num_t)rx, false) &&
            tx != rx)
        {
            twaiTx = (gpio_num_t)tx;
            twaiRx = (gpio_num_t)rx;
        }
    }
#endif

    appSetup<TWAIDriver>(std::make_unique<TWAIDriver>(twaiTx, twaiRx), "ESP32-S3 TWAI WIFI-NAG ready @ 500k");
#ifdef ESP32_DASHBOARD
    mcpDashboardSetup(appHandler.get(), appDriver.get());
#endif
}

static bool app_main_loop()
{
    bool processed = appLoop<TWAIDriver>();
#ifdef ESP32_DASHBOARD
    mcpDashboardLoop();
#endif
    return processed;
}

#if defined(ESP_PLATFORM)
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
    bool canTaskStarted = app_start_can_task();
    appStartOtaHealthObservation();
    while (true)
    {
        appServiceOtaHealthObservation();
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
}
#else
void setup()
{
    app_main_setup();
#if defined(ESP_PLATFORM)
    appStartOtaHealthObservation();
#endif
}

void loop()
{
#if defined(ESP_PLATFORM)
    appServiceOtaHealthObservation();
#endif
    app_main_loop();
}
#endif
