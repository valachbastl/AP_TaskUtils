#include "AP_TaskUtils.h"
#include "rom/ets_sys.h"

// Staticka promenna pro mutex
SemaphoreHandle_t AP_TaskUtils::_mutex = NULL;

AP_TaskUtils::AP_TaskUtils(const char *tag, uint32_t delayMs, bool useWatchdog)
    : _tag(tag), _delayMs(0), _lastRunTime(0), _startTime(0), _useWatchdog(useWatchdog), _useCompensation(true), _useNotifyDelay(false), _taskHandle(NULL)
{
    setDelay(delayMs);
}

void AP_TaskUtils::begin(bool startImmediately)
{
    _taskHandle = xTaskGetCurrentTaskHandle();

    ESP_LOGI(_tag, "Task started (watchdog: %s, immediate: %s, notifyDelay: %s)",
             _useWatchdog ? "enabled" : "disabled",
             startImmediately ? "yes" : "no",
             _useNotifyDelay ? "enabled" : "disabled");

    if (!startImmediately) {
        if (_delayMs == UINT32_MAX) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // NO_PERIOD: cekej na prvni notify pred prvnim pruchodem
        } else {
            vTaskDelay(pdMS_TO_TICKS(_delayMs));
        }
    }

    if (_useWatchdog) {
        esp_task_wdt_add(NULL);
    }
    _startTime = millis();
}

void AP_TaskUtils::delay()
{
    uint64_t now = millis();
    _lastRunTime = (uint32_t)(now - _startTime);

    uint32_t actualDelay;
    if (_useCompensation && _lastRunTime < _delayMs) {
        actualDelay = _delayMs - _lastRunTime;
    } else if (_useCompensation) {
        actualDelay = 1;
    } else {
        actualDelay = _delayMs;
    }

    if (_useWatchdog) {
        esp_task_wdt_delete(NULL);
    }

    if (_useNotifyDelay && _taskHandle) {
        TickType_t timeout = (_delayMs == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(actualDelay);
        ulTaskNotifyTake(pdTRUE, timeout);
    } else {
        vTaskDelay(pdMS_TO_TICKS(actualDelay));
    }

    if (_useWatchdog) {
        esp_task_wdt_add(NULL);
    }

    _startTime = millis();
}

uint32_t AP_TaskUtils::getLastRunTime()
{
    return _lastRunTime;
}

void AP_TaskUtils::setDelay(uint32_t delayMs)
{
    _delayMs = (delayMs > 0) ? delayMs : 1;
    if (_delayMs == UINT32_MAX) enableNotifyDelay();
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

void AP_TaskUtils::enableCompensation()
{
    _useCompensation = true;
}

void AP_TaskUtils::disableCompensation()
{
    _useCompensation = false;
}

bool AP_TaskUtils::isCompensationEnabled()
{
    return _useCompensation;
}

void AP_TaskUtils::enableNotifyDelay()
{
    _useNotifyDelay = true;
}

void AP_TaskUtils::disableNotifyDelay()
{
    if (_delayMs == UINT32_MAX) {
        ESP_LOGW(_tag, "disableNotifyDelay() ignorovano - NO_PERIOD task musi zustat probuditelny pres xTaskNotifyGive()");
        return;
    }
    _useNotifyDelay = false;
}

bool AP_TaskUtils::isNotifyDelayEnabled()
{
    return _useNotifyDelay;
}

// --- Timer metody ---

int8_t AP_TaskUtils::addTimer(uint32_t intervalMs, bool triggerOnStart)
{
    _timers.push_back({intervalMs, millis(), triggerOnStart});
    return (int8_t)(_timers.size() - 1);
}

bool AP_TaskUtils::timer(int8_t index)
{
    if (index < 0 || index >= (int8_t)_timers.size()) return false;

    if (_timers[index].firstRun) {
        _timers[index].firstRun = false;
        _timers[index].lastTrigger = millis();
        return true;
    }

    uint64_t now = millis();
    if (now - _timers[index].lastTrigger >= _timers[index].intervalMs) {
        _timers[index].lastTrigger = now;
        return true;
    }
    return false;
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

uint64_t AP_TaskUtils::seconds()
{
    return esp_timer_get_time() / 1000000;
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
