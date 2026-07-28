#include "AP_TaskUtils.h"
#include "rom/ets_sys.h"
#include <algorithm>

/* ========================================================================== */
/*  Static variables                                                           */
/* ========================================================================== */

// Both mutexes are created at static-init time (before app_main/tasks) – no init
// call, no dependency on -fthreadsafe-statics. Mutex (not spinlock) allows allocating
// std::vector inside the lock, so registry/waiters stay dynamic. Static allocation
// (StaticSemaphore_t instead of heap) can't fail due to OOM.
StaticSemaphore_t                        AP_TaskUtils::_mutexBuffer;
SemaphoreHandle_t                        AP_TaskUtils::_mutex = xSemaphoreCreateMutexStatic(&AP_TaskUtils::_mutexBuffer);
StaticSemaphore_t                        AP_TaskUtils::_registryMutexBuffer;
SemaphoreHandle_t                        AP_TaskUtils::_registryMutex = xSemaphoreCreateMutexStatic(&AP_TaskUtils::_registryMutexBuffer);
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
      _lastWakeTimeUs(esp_timer_get_time()),
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
      _intervalMs(1),  // clamp to min. 1 — prevents busy-spin if misused with DELAY; EVENT ignores it anyway
      _mode(mode),
      _useWatchdog(watchdog),
      _initialWaitDone(false),
      _readySignaled(false),
      _taskHandle(xTaskGetCurrentTaskHandle()),
      _lastWakeTimeUs(esp_timer_get_time()),
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
    RegistryEntry entry;
    entry.name      = _tag;
    entry.handle    = _taskHandle;
    entry.ready     = false;
    entry.self      = this;
    entry.busyCount = 0;
    entry.removing  = false;
    entry.drainSem  = nullptr;
    _registry.push_back(entry);
    xSemaphoreGive(_registryMutex);
}

TaskHandle_t AP_TaskUtils::_pinHandle(const char *name)
{
    xSemaphoreTake(_registryMutex, portMAX_DELAY);
    for (auto &e : _registry) {
        if (strcmp(e.name, name) == 0) {
            if (e.removing) {
                // Removal already committed (see _removeFromRegistry) — treat as gone
                // rather than racing it.
                xSemaphoreGive(_registryMutex);
                return nullptr;
            }
            e.busyCount++;
            TaskHandle_t h = e.handle;
            xSemaphoreGive(_registryMutex);
            return h;
        }
    }
    xSemaphoreGive(_registryMutex);
    return nullptr;
}

void AP_TaskUtils::_unpinHandle(const char *name)
{
    xSemaphoreTake(_registryMutex, portMAX_DELAY);
    for (auto &e : _registry) {
        if (strcmp(e.name, name) == 0) {
            e.busyCount--;
            if (e.removing && e.busyCount == 0 && e.drainSem) {
                xSemaphoreGive(e.drainSem);
            }
            break;
        }
    }
    xSemaphoreGive(_registryMutex);
}

TaskHandle_t AP_TaskUtils::_removeFromRegistry(const char *name)
{
    auto findIdx = [name]() -> int {
        for (size_t i = 0; i < _registry.size(); ++i) {
            if (strcmp(_registry[i].name, name) == 0) return (int)i;
        }
        return -1;
    };

    xSemaphoreTake(_registryMutex, portMAX_DELAY);

    int idx = findIdx();
    if (idx < 0 || _registry[idx].removing) {
        // Not found, or a concurrent destroy()/destroy(name) already claimed removal
        // (caller-side double-destroy) — treat as already gone rather than racing it.
        xSemaphoreGive(_registryMutex);
        return nullptr;
    }

    _registry[idx].removing = true;
    TaskHandle_t h = _registry[idx].handle;

    while (_registry[idx].busyCount > 0) {
        // notify(name)/suspend(name)/resume(name) still in flight on this handle —
        // wait for it to finish before the handle can be safely deleted/reused.
        // Lazily created: only hit under real concurrency.
        if (!_registry[idx].drainSem) _registry[idx].drainSem = xSemaphoreCreateBinary();
        SemaphoreHandle_t drain = _registry[idx].drainSem;
        xSemaphoreGive(_registryMutex);

        if (drain) {
            xSemaphoreTake(drain, portMAX_DELAY);
        } else {
            // OOM fallback — couldn't allocate the drain semaphore, poll instead of
            // blocking on a null handle (which would be undefined behavior).
            ESP_LOGW("AP_TaskUtils", "_removeFromRegistry('%s'): drain semaphore alloc failed, polling", name);
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        xSemaphoreTake(_registryMutex, portMAX_DELAY);
        idx = findIdx();  // vector may have reallocated while unlocked; removing==true
                           // guarantees the entry itself is still present
    }

    // Safe now — no in-flight name-based op holds this handle. Cancel the owning
    // instance's notifyAfter() timer and wake any waitReady() callers blocked on
    // this name in the SAME critical section, so a concurrent signalReady() can
    // never race the erase (give-then-delete vs. delete-then-give).
    RegistryEntry &e = _registry[idx];
    if (e.self && e.self->_oneShot) {
        esp_timer_stop(e.self->_oneShot);
        esp_timer_delete(e.self->_oneShot);
        e.self->_oneShot = nullptr;
    }
    if (e.drainSem) vSemaphoreDelete(e.drainSem);

    for (const auto &w : _waiters) {
        if (strcmp(w.name, name) == 0) {
            if (w.targetDestroyed) *w.targetDestroyed = true;
            xSemaphoreGive(w.sem);
        }
    }

    _registry.erase(_registry.begin() + idx);
    xSemaphoreGive(_registryMutex);
    return h;
}

/* ========================================================================== */
/*  Core methods                                                               */
/* ========================================================================== */

void AP_TaskUtils::signalReady()
{
    if (_readySignaled) return;
    _readySignaled = true;

    // Scan and give under the same lock — giving after releasing the lock would let
    // a waitReady() timeout race in between (delete its semaphore before this give),
    // producing a use-after-free. xSemaphoreGive is O(1) and never blocks, so holding
    // the lock a little longer here is fine.
    xSemaphoreTake(_registryMutex, portMAX_DELAY);
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
    xSemaphoreGive(_registryMutex);
}

// Caller must hold a self-pin (_pinHandle(_tag) ... _unpinHandle(_tag)) before
// calling — see _waitForTimer()/notifyAfter(). Not safe to call unprotected: a
// concurrent destroy(name)/destroy() targeting us deletes _oneShot under the same
// pin/drain mechanism that already guards notify/suspend/resume(name).
void AP_TaskUtils::_ensureOneShot()
{
    if (_oneShot) return;

    esp_timer_create_args_t args = {};
    args.callback = [](void *arg) {
        AP_TaskUtils *self = static_cast<AP_TaskUtils *>(arg);
        self->_timerFired = true;
        xTaskNotifyGive(self->_taskHandle);
    };
    args.arg  = this;
    args.name = "AP_TaskUtils";
    if (esp_timer_create(&args, &_oneShot) != ESP_OK) {
        ESP_LOGW(_tag, "esp_timer_create failed — falling back to tick-based wait for this cycle");
        _oneShot = nullptr;
    }
}

// Arms _oneShot for durationUs and blocks until it fires (or, for DELAY, until any
// external notify() arrives).
void AP_TaskUtils::_waitForTimer(int64_t durationUs, bool ignoreExternalNotify)
{
    if (durationUs < 0) durationUs = 0;

    // Pin our own registry entry while touching _oneShot — the same busyCount/
    // drain mechanism _removeFromRegistry() already uses for notify/suspend/
    // resume(name) now also blocks a concurrent destroy(name)/destroy() from
    // deleting _oneShot out from under this create-and-arm sequence. Released
    // before the blocking wait below, so destroy(name) never has to wait out a
    // full interval — only this short arm sequence.
    bool armed = false;
    if (_pinHandle(_tag)) {
        _ensureOneShot();
        if (_oneShot) {
            _timerFired = false;
            esp_timer_stop(_oneShot);
            esp_timer_start_once(_oneShot, (uint64_t)durationUs);
            armed = true;
        }
        _unpinHandle(_tag);
    }

    if (!armed) {
        // No usable timer — esp_timer_create() failed (OOM), or a concurrent
        // destroy() has already claimed removal of this task. Degrade to a
        // tick-based wait rather than blocking forever; still subject to
        // tick-rate drift, but only in these two rare fallback cases.
        vTaskDelay(pdMS_TO_TICKS((uint32_t)(durationUs / 1000)));
        return;
    }

    do {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    } while (ignoreExternalNotify && !_timerFired);
}

void AP_TaskUtils::_blockForMode()
{
    switch (_mode) {
        case PERIODIC: {
            // Same catch-up semantics as vTaskDelayUntil: always advance the
            // reference by exactly one interval, then block only if that's still
            // in the future. If we're already behind, return immediately instead
            // of bursting through multiple missed periods. notify() is ignored
            // here — matches the old vTaskDelayUntil, which task notifications
            // never interrupted.
            int64_t nowUs = esp_timer_get_time();
            _lastWakeTimeUs += (int64_t)_intervalMs * 1000;
            if (_lastWakeTimeUs > nowUs) {
                _waitForTimer(_lastWakeTimeUs - nowUs, /*ignoreExternalNotify=*/true);
            }
            break;
        }
        case DELAY:
            _waitForTimer((int64_t)_intervalMs * 1000, /*ignoreExternalNotify=*/false);
            break;
        case EVENT:
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            break;
    }
}

void AP_TaskUtils::wait()
{
    signalReady();

    _lastRunTime = (uint32_t)(millis() - _cycleStart);

    if (_useWatchdog) esp_task_wdt_delete(nullptr);

    _blockForMode();

    if (_useWatchdog) esp_task_wdt_add(nullptr);

    _cycleStart = millis();
}

void AP_TaskUtils::sleep()
{
    signalReady();

    _lastRunTime = (uint32_t)(millis() - _cycleStart);

    if (_useWatchdog) esp_task_wdt_delete(nullptr);

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (_mode == PERIODIC) _lastWakeTimeUs = esp_timer_get_time();

    if (_useWatchdog) esp_task_wdt_add(nullptr);

    _cycleStart = millis();
}

void AP_TaskUtils::notifyAfter(uint32_t ms)
{
    // Reusable one-shot timer — created once, restarted on each call. No leak.
    // Pinned the same way as _waitForTimer(): protects _oneShot from a concurrent
    // destroy(name)/destroy() targeting us, released before the blocking sleep()
    // below so destroy(name) isn't held up for the full delay.
    bool armed = false;
    if (_pinHandle(_tag)) {
        _ensureOneShot();
        if (_oneShot) {
            // esp_timer_stop first is harmless if already stopped.
            esp_timer_stop(_oneShot);
            esp_timer_start_once(_oneShot, (uint64_t)ms * 1000ULL);
            armed = true;
        }
        _unpinHandle(_tag);
    }

    if (!armed) {
        sleep();  // fallback — wait for external notify() only
        return;
    }

    // Block — woken by the timer or an earlier external notify()
    sleep();

    // Cancel the timer if we were woken externally before it fired, so no stale
    // notification lingers for the next wait()/sleep(). Pinned again — destroy()
    // may have deleted _oneShot while we were blocked in sleep().
    if (_pinHandle(_tag)) {
        if (_oneShot) esp_timer_stop(_oneShot);
        _unpinHandle(_tag);
    }
}

void AP_TaskUtils::waitEvent(EventGroupHandle_t group, EventBits_t bits)
{
    signalReady();

    _lastRunTime = (uint32_t)(millis() - _cycleStart);

    if (_useWatchdog) esp_task_wdt_delete(nullptr);
    xEventGroupWaitBits(group, bits, pdFALSE, pdTRUE, portMAX_DELAY);
    if (_useWatchdog) esp_task_wdt_add(nullptr);

    // Restart the PERIODIC base time, same as sleep() — otherwise the next wait()
    // would "catch up" all cycles missed while blocked here.
    if (_mode == PERIODIC) _lastWakeTimeUs = esp_timer_get_time();

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
            if (_mode == PERIODIC) _lastWakeTimeUs = esp_timer_get_time();
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

    // Target-destroyed flag — lives on this stack frame, referenced by the
    // WaiterEntry so _removeFromRegistry() can tell us the task will never
    // become ready instead of silently giving the semaphore as if it had.
    volatile bool targetDestroyed = false;

    xSemaphoreTake(_registryMutex, portMAX_DELAY);
    for (const auto &e : _registry) {
        if (strcmp(e.name, name) == 0 && e.ready) {
            xSemaphoreGive(_registryMutex);
            vSemaphoreDelete(sem);
            if (_mode == PERIODIC) _lastWakeTimeUs = esp_timer_get_time();
            _cycleStart = millis();
            return true;
        }
    }
    _waiters.push_back({name, sem, &targetDestroyed});
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

    if (_mode == PERIODIC) _lastWakeTimeUs = esp_timer_get_time();
    _cycleStart = millis();

    if (ok && targetDestroyed) {
        ESP_LOGW("AP_TaskUtils", "waitReady('%s'): target task was destroyed before becoming ready", name);
        return false;
    }
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

    _blockForMode();

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
    // Waits out any in-flight notify(name)/suspend(name)/resume(name) on our own
    // name, cancels our notifyAfter() timer, wakes waitReady() waiters, and removes
    // us from the registry — all before vTaskDelete(nullptr) makes our handle
    // invalid/reusable.
    _removeFromRegistry(_tag);
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
    TaskHandle_t h = _pinHandle(name);
    if (!h) {
        ESP_LOGW("AP_TaskUtils", "notify('%s'): task not found in registry", name);
        return false;
    }
    xTaskNotifyGive(h);
    _unpinHandle(name);
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
        if (esp_timer_start_once(ctx->timer, (uint64_t)ms * 1000ULL) != ESP_OK) {
            ESP_LOGW("AP_TaskUtils", "notifyAfter('%s'): timer start failed", name);
            esp_timer_delete(ctx->timer);
            delete ctx;
        }
    } else {
        ESP_LOGW("AP_TaskUtils", "notifyAfter('%s'): timer create failed", name);
        delete ctx;
    }
}

bool AP_TaskUtils::destroy(const char *name)
{
    // _removeFromRegistry waits out any in-flight notify/suspend/resume(name) and
    // any concurrent self-destroy() before handing back the handle, so it's still
    // guaranteed valid here — closes the TOCTOU window that used to exist between
    // finding the handle and deleting it.
    TaskHandle_t h = _removeFromRegistry(name);
    if (!h) {
        ESP_LOGW("AP_TaskUtils", "destroy('%s'): task not found in registry", name);
        return false;
    }
    esp_task_wdt_delete(h);   // unregister from TWDT (harmless if not registered) — otherwise the watchdog would panic after the task is deleted
    vTaskDelete(h);
    return true;
}

bool AP_TaskUtils::suspend(const char *name)
{
    TaskHandle_t h = _pinHandle(name);
    if (!h) {
        ESP_LOGW("AP_TaskUtils", "suspend('%s'): task not found in registry", name);
        return false;
    }
    vTaskSuspend(h);
    _unpinHandle(name);
    return true;
}

bool AP_TaskUtils::resume(const char *name)
{
    TaskHandle_t h = _pinHandle(name);
    if (!h) {
        ESP_LOGW("AP_TaskUtils", "resume('%s'): task not found in registry", name);
        return false;
    }
    vTaskResume(h);
    _unpinHandle(name);
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
    // No-op since v2.5.0 — _mutex is now created at static-init time, same as
    // _registryMutex, so it can never be null. Kept for source compatibility with
    // existing calls.
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
