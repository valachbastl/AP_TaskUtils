#include "AP_TaskUtils.h"
#include "rom/ets_sys.h"
#include <algorithm>

/* ========================================================================== */
/*  Static variables                                                           */
/* ========================================================================== */

SemaphoreHandle_t                        AP_TaskUtils::_mutex    = nullptr;
std::vector<AP_TaskUtils::RegistryEntry> AP_TaskUtils::_registry = {};
std::vector<AP_TaskUtils::WaiterEntry>   AP_TaskUtils::_waiters  = {};

/* ========================================================================== */
/*  Constructors                                                               */
/* ========================================================================== */

AP_TaskUtils::AP_TaskUtils(const char *tag, uint32_t intervalMs, Mode mode,
                           bool watchdog, bool waitBeforeStart)
    : _tag(tag),
      _intervalMs(intervalMs > 0 ? intervalMs : 1),
      _mode(mode),
      _useWatchdog(watchdog),
      _initialWaitDone(false),
      _readySignaled(false),
      _taskHandle(xTaskGetCurrentTaskHandle()),
      _lastWakeTime(xTaskGetTickCount()),
      _cycleStart(millis()),
      _lastRunTime(0)
{
    _registerTask();
    if (_useWatchdog) esp_task_wdt_add(nullptr);

    ESP_LOGI(_tag, "Task initialized (mode: %s, interval: %lu ms, watchdog: %s)",
             _modeStr(), (unsigned long)_intervalMs, _useWatchdog ? "on" : "off");

    if (waitBeforeStart) this->waitBeforeStart();
}

AP_TaskUtils::AP_TaskUtils(const char *tag, Mode mode, bool watchdog, bool waitBeforeStart)
    : _tag(tag),
      _intervalMs(0),
      _mode(mode),
      _useWatchdog(watchdog),
      _initialWaitDone(false),
      _readySignaled(false),
      _taskHandle(xTaskGetCurrentTaskHandle()),
      _lastWakeTime(xTaskGetTickCount()),
      _cycleStart(millis()),
      _lastRunTime(0)
{
    if (mode != EVENT) {
        ESP_LOGW(_tag, "This constructor is intended for EVENT mode — use the interval constructor for PERIODIC/DELAY.");
    }

    _registerTask();
    if (_useWatchdog) esp_task_wdt_add(nullptr);

    ESP_LOGI(_tag, "Task initialized (mode: %s, watchdog: %s)",
             _modeStr(), _useWatchdog ? "on" : "off");

    if (waitBeforeStart) this->waitBeforeStart();
}

/* ========================================================================== */
/*  Registry                                                                   */
/* ========================================================================== */

void AP_TaskUtils::_registerTask()
{
    for (const auto &e : _registry) {
        if (strcmp(e.name, _tag) == 0) {
            ESP_LOGE(_tag, "Task '%s' is already registered — duplicate name!", _tag);
            return;
        }
    }
    _registry.push_back({_tag, _taskHandle, false});
}

void AP_TaskUtils::_unregisterByHandle(TaskHandle_t handle)
{
    _registry.erase(
        std::remove_if(_registry.begin(), _registry.end(),
            [handle](const RegistryEntry &e) { return e.handle == handle; }),
        _registry.end()
    );
}

TaskHandle_t AP_TaskUtils::_findHandle(const char *name)
{
    for (const auto &e : _registry) {
        if (strcmp(e.name, name) == 0) return e.handle;
    }
    return nullptr;
}

/* ========================================================================== */
/*  Core methods                                                               */
/* ========================================================================== */

void AP_TaskUtils::signalReady()
{
    if (_readySignaled) return;
    _readySignaled = true;

    for (auto &e : _registry) {
        if (e.handle == _taskHandle) {
            e.ready = true;
            break;
        }
    }

    for (const auto &w : _waiters) {
        if (strcmp(w.name, _tag) == 0) {
            xSemaphoreGive(w.sem);
        }
    }
}

void AP_TaskUtils::wait()
{
    signalReady();

    _lastRunTime = (uint32_t)(millis() - _cycleStart);

    if (_useWatchdog) esp_task_wdt_delete(nullptr);

    switch (_mode) {
        case PERIODIC:
            vTaskDelayUntil(&_lastWakeTime, pdMS_TO_TICKS(_intervalMs));
            break;
        case DELAY:
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(_intervalMs));
            break;
        case EVENT:
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            break;
    }

    if (_useWatchdog) esp_task_wdt_add(nullptr);

    _cycleStart = millis();
}

void AP_TaskUtils::waitBeforeStart()
{
    if (_initialWaitDone) {
        ESP_LOGW(_tag, "waitBeforeStart() ignored — initial wait already done.");
        return;
    }

    if (_mode == EVENT) signalReady();  // signal ready before blocking — others can notify() immediately

    if (_useWatchdog) esp_task_wdt_delete(nullptr);

    switch (_mode) {
        case PERIODIC:
            vTaskDelayUntil(&_lastWakeTime, pdMS_TO_TICKS(_intervalMs));
            break;
        case DELAY:
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(_intervalMs));
            break;
        case EVENT:
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            break;
    }

    if (_useWatchdog) esp_task_wdt_add(nullptr);

    _cycleStart      = millis();
    _initialWaitDone = true;
}

void AP_TaskUtils::notify()
{
    xTaskNotifyGive(_taskHandle);
}

void AP_TaskUtils::destroy()
{
    disableWatchdog();
    _unregisterByHandle(_taskHandle);
    vTaskDelete(nullptr);
}

void AP_TaskUtils::suspend()
{
    vTaskSuspend(_taskHandle);
}

void AP_TaskUtils::resume()
{
    vTaskResume(_taskHandle);
}

/* ========================================================================== */
/*  Watchdog                                                                   */
/* ========================================================================== */

void AP_TaskUtils::enableWatchdog()
{
    if (!_useWatchdog) {
        _useWatchdog = true;
        esp_task_wdt_add(nullptr);
        ESP_LOGI(_tag, "Watchdog enabled");
    }
}

void AP_TaskUtils::disableWatchdog()
{
    if (_useWatchdog) {
        _useWatchdog = false;
        esp_task_wdt_delete(nullptr);
        ESP_LOGI(_tag, "Watchdog disabled");
    }
}

bool AP_TaskUtils::isWatchdogEnabled() const
{
    return _useWatchdog;
}

void AP_TaskUtils::feedWatchdog()
{
    if (_useWatchdog) esp_task_wdt_reset();
}

/* ========================================================================== */
/*  Configuration                                                              */
/* ========================================================================== */

void AP_TaskUtils::setInterval(uint32_t intervalMs)
{
    _intervalMs = (intervalMs > 0) ? intervalMs : 1;
}

uint32_t AP_TaskUtils::getInterval() const
{
    return _intervalMs;
}

uint32_t AP_TaskUtils::getLastRunTime() const
{
    return _lastRunTime;
}

/* ========================================================================== */
/*  Timers                                                                     */
/* ========================================================================== */

int AP_TaskUtils::addTimer(uint32_t intervalMs, bool triggerOnStart)
{
    _timers.push_back({intervalMs, millis(), triggerOnStart});
    return (int)(_timers.size() - 1);
}

bool AP_TaskUtils::timer(int index)
{
    if (index < 0 || index >= (int)_timers.size()) return false;

    TimerEntry &t = _timers[index];

    if (t.firstRun) {
        t.firstRun    = false;
        t.lastTrigger = millis();
        return true;
    }

    uint64_t now = millis();
    if (now - t.lastTrigger >= t.intervalMs) {
        t.lastTrigger = now;
        return true;
    }
    return false;
}

/* ========================================================================== */
/*  Static — cross-task operations                                             */
/* ========================================================================== */

bool AP_TaskUtils::notify(const char *name)
{
    TaskHandle_t h = _findHandle(name);
    if (!h) {
        ESP_LOGW("AP_TaskUtils", "notify('%s'): task not found in registry", name);
        return false;
    }
    xTaskNotifyGive(h);
    return true;
}

bool AP_TaskUtils::destroy(const char *name)
{
    TaskHandle_t h = _findHandle(name);
    if (!h) {
        ESP_LOGW("AP_TaskUtils", "destroy('%s'): task not found in registry", name);
        return false;
    }
    _unregisterByHandle(h);
    vTaskDelete(h);
    return true;
}

bool AP_TaskUtils::suspend(const char *name)
{
    TaskHandle_t h = _findHandle(name);
    if (!h) {
        ESP_LOGW("AP_TaskUtils", "suspend('%s'): task not found in registry", name);
        return false;
    }
    vTaskSuspend(h);
    return true;
}

bool AP_TaskUtils::resume(const char *name)
{
    TaskHandle_t h = _findHandle(name);
    if (!h) {
        ESP_LOGW("AP_TaskUtils", "resume('%s'): task not found in registry", name);
        return false;
    }
    vTaskResume(h);
    return true;
}

bool AP_TaskUtils::waitReady(const char *name, uint32_t timeoutMs)
{
    // Task is already ready — no need to wait
    for (const auto &e : _registry) {
        if (strcmp(e.name, name) == 0 && e.ready) return true;
    }

    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    _waiters.push_back({name, sem});

    TickType_t ticks = (timeoutMs == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    bool ok = xSemaphoreTake(sem, ticks) == pdTRUE;

    _waiters.erase(
        std::remove_if(_waiters.begin(), _waiters.end(),
            [sem](const WaiterEntry &e) { return e.sem == sem; }),
        _waiters.end()
    );
    vSemaphoreDelete(sem);

    if (!ok) ESP_LOGW("AP_TaskUtils", "waitReady('%s'): timeout", name);
    return ok;
}

/* ========================================================================== */
/*  Static — utilities                                                         */
/* ========================================================================== */

void AP_TaskUtils::initWatchdog(uint32_t timeoutMs, bool panic)
{
    esp_task_wdt_deinit();

    esp_task_wdt_config_t config = {
        .timeout_ms     = timeoutMs,
        .idle_core_mask = 0,
        .trigger_panic  = panic
    };
    esp_task_wdt_init(&config);

    ESP_LOGI("AP_TaskUtils", "Watchdog initialized (timeout: %lu ms, panic: %s)",
             (unsigned long)timeoutMs, panic ? "yes" : "no");
}

uint64_t AP_TaskUtils::seconds()
{
    return esp_timer_get_time() / 1000000ULL;
}

uint64_t AP_TaskUtils::millis()
{
    return esp_timer_get_time() / 1000ULL;
}

uint64_t AP_TaskUtils::micros()
{
    return (uint64_t)esp_timer_get_time();
}

void AP_TaskUtils::delayMs(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void AP_TaskUtils::delayUs(uint32_t us)
{
    ets_delay_us(us);
}

/* ========================================================================== */
/*  Static — global mutex                                                      */
/* ========================================================================== */

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

/* ========================================================================== */
/*  Helpers                                                                    */
/* ========================================================================== */

const char *AP_TaskUtils::_modeStr() const
{
    switch (_mode) {
        case PERIODIC: return "PERIODIC";
        case DELAY:    return "DELAY";
        case EVENT:    return "EVENT";
    }
    return "UNKNOWN";
}
