#include "AP_TaskUtils.h"
#include "rom/ets_sys.h"

// Staticka promenna pro mutex
SemaphoreHandle_t AP_TaskUtils::_mutex = NULL;

AP_TaskUtils::AP_TaskUtils(const char *tag, uint32_t delayMs, bool useWatchdog)
    : _tag(tag), _delayMs(delayMs), _useWatchdog(useWatchdog)
{
}

void AP_TaskUtils::begin()
{
    ESP_LOGI(_tag, "Task started (watchdog: %s)", _useWatchdog ? "enabled" : "disabled");
    if (_useWatchdog) {
        esp_task_wdt_add(NULL);
    }
}

void AP_TaskUtils::delay()
{
    if (_useWatchdog) {
        esp_task_wdt_delete(NULL);
    }
    vTaskDelay(pdMS_TO_TICKS(_delayMs));
    if (_useWatchdog) {
        esp_task_wdt_add(NULL);
    }
}

void AP_TaskUtils::setDelay(uint32_t delayMs)
{
    _delayMs = (delayMs > 0) ? delayMs : 1;
}

uint32_t AP_TaskUtils::getDelay()
{
    return _delayMs;
}

void AP_TaskUtils::feedWatchdog()
{
    if (_useWatchdog) {
        esp_task_wdt_reset();
    }
}

void AP_TaskUtils::enableWatchdog()
{
    if (!_useWatchdog) {
        _useWatchdog = true;
        esp_task_wdt_add(NULL);
        ESP_LOGI(_tag, "Watchdog enabled");
    }
}

void AP_TaskUtils::disableWatchdog()
{
    if (_useWatchdog) {
        esp_task_wdt_delete(NULL);
        _useWatchdog = false;
        ESP_LOGI(_tag, "Watchdog disabled");
    }
}

bool AP_TaskUtils::isWatchdogEnabled()
{
    return _useWatchdog;
}

// --- Staticke metody ---

void AP_TaskUtils::initWatchdog(uint32_t timeoutMs, bool panic)
{
    esp_task_wdt_deinit();

    esp_task_wdt_config_t config = {
        .timeout_ms = timeoutMs,
        .idle_core_mask = 0,
        .trigger_panic = panic
    };

    esp_task_wdt_init(&config);
    ESP_LOGI("AP_TaskUtils", "Watchdog initialized (timeout: %lu ms, panic: %s)",
             timeoutMs, panic ? "yes" : "no");
}

uint64_t AP_TaskUtils::millis()
{
    return esp_timer_get_time() / 1000;
}

uint64_t AP_TaskUtils::micros()
{
    return esp_timer_get_time();
}

void AP_TaskUtils::delayMs(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void AP_TaskUtils::delayUs(uint32_t us)
{
    ets_delay_us(us);
}

// --- Mutex metody ---

void AP_TaskUtils::initMutex()
{
    _mutex = xSemaphoreCreateMutex();
    ESP_LOGI("AP_TaskUtils", "Mutex initialized");
}

bool AP_TaskUtils::lock()
{
    return xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE;
}

bool AP_TaskUtils::lock(uint32_t timeoutMs)
{
    return xSemaphoreTake(_mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void AP_TaskUtils::unlock()
{
    xSemaphoreGive(_mutex);
}
