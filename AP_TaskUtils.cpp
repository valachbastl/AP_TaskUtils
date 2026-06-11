#include "AP_TaskUtils.h"
#include "rom/ets_sys.h"
#include <algorithm>

/* ========================================================================== */
/*  Static variables                                                           */
/* ========================================================================== */

SemaphoreHandle_t                        AP_TaskUtils::_mutex      = nullptr;
// Registry mutex se vytvori uz pri static-init (pred app_main/tasky) – zadny init call,
// zadna zavislost na -fthreadsafe-statics. Mutex (ne spinlock) dovoluje alokaci
// std::vector uvnitr zamku, takze registry/waiters zustavaji dynamicke.
SemaphoreHandle_t                        AP_TaskUtils::_registryMutex = xSemaphoreCreateMutex();
std::vector<AP_TaskUtils::RegistryEntry> AP_TaskUtils::_registry   = {};
std::vector<AP_TaskUtils::WaiterEntry>   AP_TaskUtils::_waiters    = {};

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
    xSemaphoreTake(_registryMutex, portMAX_DELAY);
    for (const auto &e : _registry) {
        if (strcmp(e.name, _tag) == 0) {
            xSemaphoreGive(_registryMutex);
            ESP_LOGE(_tag, "Task '%s' is already registered — duplicate name!", _tag);
            return;
        }
    }
    _registry.push_back({_tag, _taskHandle, false});
    xSemaphoreGive(_registryMutex);
}

void AP_TaskUtils::_unregisterByHandle(TaskHandle_t handle)
{
    xSemaphoreTake(_registryMutex, portMAX_DELAY);
    _registry.erase(
        std::remove_if(_registry.begin(), _registry.end(),
            [handle](const RegistryEntry &e) { return e.handle == handle; }),
        _registry.end()
    );
    xSemaphoreGive(_registryMutex);
}

TaskHandle_t AP_TaskUtils::_findHandle(const char *name)
{
    xSemaphoreTake(_registryMutex, portMAX_DELAY);
    for (const auto &e : _registry) {
        if (strcmp(e.name, name) == 0) {
            TaskHandle_t h = e.handle;
            xSemaphoreGive(_registryMutex);
            return h;
        }
    }
    xSemaphoreGive(_registryMutex);
    return nullptr;
}

/* ========================================================================== */
/*  Core methods                                                               */
/* ========================================================================== */

void AP_TaskUtils::signalReady()
{
    if (_readySignaled) return;
    _readySignaled = true;

    // Collect semaphores under the lock, give them after releasing it so we never
    // hold the registry mutex while a woken higher-priority task runs. Dynamic vector
    // → no fixed cap on the number of waiters.
    std::vector<SemaphoreHandle_t> toNotify;

    xSemaphoreTake(_registryMutex, portMAX_DELAY);
    for (auto &e : _registry) {
        if (e.handle == _taskHandle) {
            e.ready = true;
            break;
        }
    }
    for (const auto &w : _waiters) {
        if (strcmp(w.name, _tag) == 0) {
            toNotify.push_back(w.sem);
        }
    }
    xSemaphoreGive(_registryMutex);

    for (auto sem : toNotify) {
        xSemaphoreGive(sem);
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

void AP_TaskUtils::sleep()
{
    signalReady();

    _lastRunTime = (uint32_t)(millis() - _cycleStart);

    if (_useWatchdog) esp_task_wdt_delete(nullptr);

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (_mode == PERIODIC) _lastWakeTime = xTaskGetTickCount();

    if (_useWatchdog) esp_task_wdt_add(nullptr);

    _cycleStart = millis();
}

void AP_TaskUtils::notifyAfter(uint32_t ms)
{
    // Reusable one-shot timer — created once, restarted on each call. No leak.
    if (!_oneShot) {
        esp_timer_create_args_t args = {};
        args.callback = [](void *arg) { xTaskNotifyGive((TaskHandle_t)arg); };
        args.arg      = (void *)_taskHandle;
        args.name     = "AP_notifyAfter";
        if (esp_timer_create(&args, &_oneShot) != ESP_OK) {
            ESP_LOGW(_tag, "notifyAfter: timer create failed");
            sleep();  // fallback — wait for external notify() only
            return;
        }
    }

    // Arm the timer (esp_timer_stop first is harmless if already stopped).
    esp_timer_stop(_oneShot);
    esp_timer_start_once(_oneShot, (uint64_t)ms * 1000ULL);

    // Block — woken by the timer or an earlier external notify()
    sleep();

    // Cancel the timer if we were woken externally before it fired,
    // so no stale notification lingers for the next wait()/sleep().
    esp_timer_stop(_oneShot);
}

void AP_TaskUtils::waitEvent(EventGroupHandle_t group, EventBits_t bits)
{
    signalReady();

    _lastRunTime = (uint32_t)(millis() - _cycleStart);

    if (_useWatchdog) esp_task_wdt_delete(nullptr);
    xEventGroupWaitBits(group, bits, pdFALSE, pdTRUE, portMAX_DELAY);
    if (_useWatchdog) esp_task_wdt_add(nullptr);

    _cycleStart = millis();
}

bool AP_TaskUtils::waitReady(const char *name, uint32_t timeoutMs)
{
    signalReady();

    _lastRunTime = (uint32_t)(millis() - _cycleStart);

    // Fast path — already ready, no semaphore needed
    xSemaphoreTake(_registryMutex, portMAX_DELAY);
    for (const auto &e : _registry) {
        if (strcmp(e.name, name) == 0 && e.ready) {
            xSemaphoreGive(_registryMutex);
            _cycleStart = millis();
            return true;
        }
    }
    xSemaphoreGive(_registryMutex);

    // Create semaphore outside the lock, then register under lock.
    // Re-check ready after lock to close the window between the two sections.
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    if (sem == nullptr) {
        ESP_LOGE("AP_TaskUtils", "waitReady('%s'): semaphore alloc failed", name);
        _cycleStart = millis();
        return false;
    }

    xSemaphoreTake(_registryMutex, portMAX_DELAY);
    for (const auto &e : _registry) {
        if (strcmp(e.name, name) == 0 && e.ready) {
            xSemaphoreGive(_registryMutex);
            vSemaphoreDelete(sem);
            _cycleStart = millis();
            return true;
        }
    }
    _waiters.push_back({name, sem});
    xSemaphoreGive(_registryMutex);

    if (_useWatchdog) esp_task_wdt_delete(nullptr);

    TickType_t ticks = (timeoutMs == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    bool ok = xSemaphoreTake(sem, ticks) == pdTRUE;

    if (_useWatchdog) esp_task_wdt_add(nullptr);

    xSemaphoreTake(_registryMutex, portMAX_DELAY);
    _waiters.erase(
        std::remove_if(_waiters.begin(), _waiters.end(),
            [sem](const WaiterEntry &e) { return e.sem == sem; }),
        _waiters.end()
    );
    xSemaphoreGive(_registryMutex);

    vSemaphoreDelete(sem);

    _cycleStart = millis();

    if (!ok) ESP_LOGW("AP_TaskUtils", "waitReady('%s'): timeout", name);
    return ok;
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
    if (_oneShot) {
        esp_timer_stop(_oneShot);
        esp_timer_delete(_oneShot);
        _oneShot = nullptr;
    }
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

void AP_TaskUtils::notifyAfter(const char *name, uint32_t ms)
{
    // Fire-and-forget — context holds the timer so the callback can self-delete it.
    // Deleting a one-shot timer from its own callback is safe with the default task
    // dispatch: the timer is already removed from the active list before the callback runs.
    struct OneShotCtx { esp_timer_handle_t timer; const char *name; };
    OneShotCtx *ctx = new OneShotCtx{nullptr, name};

    esp_timer_create_args_t args = {};
    args.callback = [](void *arg) {
        OneShotCtx *c = static_cast<OneShotCtx *>(arg);
        AP_TaskUtils::notify(c->name);
        esp_timer_delete(c->timer);
        delete c;
    };
    args.arg  = ctx;
    args.name = "AP_notifyAfter";

    if (esp_timer_create(&args, &ctx->timer) == ESP_OK) {
        esp_timer_start_once(ctx->timer, (uint64_t)ms * 1000ULL);
    } else {
        ESP_LOGW("AP_TaskUtils", "notifyAfter('%s'): timer create failed", name);
        delete ctx;
    }
}

bool AP_TaskUtils::destroy(const char *name)
{
    TaskHandle_t h = _findHandle(name);
    if (!h) {
        ESP_LOGW("AP_TaskUtils", "destroy('%s'): task not found in registry", name);
        return false;
    }
    esp_task_wdt_delete(h);   // odhlasit z TWDT (neskodne kdyz neni prihlasen) — jinak by po smazani tasku watchdog panicoval
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
