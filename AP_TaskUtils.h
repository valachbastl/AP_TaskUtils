#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <vector>
#include <cstring>

#include "AP_Channel.h"
#include "AP_Queue.h"

/* ========================================================================== */
/*  AP_TaskUtils                                                               */
/*                                                                             */
/*  Helper for FreeRTOS tasks — lifecycle management, watchdog, timers,       */
/*  cross-task registry (notify/suspend/resume/destroy by name).              */
/*                                                                             */
/*  Recommended pattern for sharing the task name across the file:            */
/*                                                                             */
/*    static const char *TAG = "mqttTask";                                    */
/*                                                                             */
/*    static void mqttTask(void *pvParameters) {                              */
/*        AP_TaskUtils task(TAG, 60000, AP_TaskUtils::DELAY);                 */
/*        ESP_LOGI(TAG, "start");                                              */
/*        while (1) {                                                          */
/*            task.wait();                                                     */
/*        }                                                                    */
/*    }                                                                        */
/*                                                                             */
/*    void mqtt_task_create() {                                                */
/*        xTaskCreatePinnedToCore(mqttTask, TAG, 4096, NULL, 5, NULL, 1);     */
/*    }                                                                        */
/* ========================================================================== */

class AP_TaskUtils
{
public:
    enum Mode {
        PERIODIC,  // esp_timer-scheduled — precise interval, automatic run time compensation
        DELAY,     // esp_timer-scheduled — approximate interval, wakeable anytime
        EVENT      // ulTaskNotifyTake(∞) — event-driven only, no interval
    };

    /**
     * @brief Constructor for PERIODIC and DELAY mode
     * @param tag             Task name (used for logging, registry and xTaskCreate)
     * @param intervalMs      Interval in ms
     * @param mode            Task mode (default PERIODIC)
     * @param watchdog        Enable watchdog (default true)
     * @param waitBeforeStart Wait one interval before first loop iteration (default false)
     */
    AP_TaskUtils(const char *tag, uint32_t intervalMs, Mode mode = PERIODIC,
                 bool watchdog = true, bool waitBeforeStart = false);

    /**
     * @brief Constructor for EVENT mode (no interval)
     * @param tag             Task name (used for logging, registry and xTaskCreate)
     * @param mode            AP_TaskUtils::EVENT
     * @param watchdog        Enable watchdog (default true)
     * @param waitBeforeStart Wait for first notification before first loop iteration (default true)
     */
    AP_TaskUtils(const char *tag, Mode mode, bool watchdog = true, bool waitBeforeStart = true);

    AP_TaskUtils(const AP_TaskUtils &) = delete;
    AP_TaskUtils &operator=(const AP_TaskUtils &) = delete;

    // -------------------------------------------------------------------------
    //  Core methods
    // -------------------------------------------------------------------------

    /**
     * @brief Main blocking call — put at end of while(1) loop.
     *        Calls signalReady() if not yet called.
     *
     *        PERIODIC → esp_timer-scheduled wake, precise interval, compensates run
     *                   time (same catch-up semantics as vTaskDelayUntil); notify()
     *                   has no effect on timing, same as before
     *        DELAY    → esp_timer-scheduled wake with the configured interval,
     *                   wakeable early by any notify()
     *        EVENT    → ulTaskNotifyTake without timeout (woken exclusively by notify())
     *
     *        PERIODIC/DELAY timing is driven by the monotonic esp_timer, not the
     *        FreeRTOS tick — immune to tick-rate drift under dynamic frequency
     *        scaling (a known ESP-IDF/FreeRTOS limitation, see espressif/esp-idf#17992).
     */
    void wait();

    /**
     * @brief Blocks the task until notify() arrives — no timeout, regardless of mode.
     *        Works anywhere in the task body (before the loop or inside while(1)).
     *        Watchdog is handled the same way as in wait() — temporarily unregistered.
     *        On wakeup the PERIODIC timer is reset, so wait() continues correctly.
     *
     *        Typical usage — wait for a network event before the first cycle:
     *          task.sleep();       // sleeps until ethernet_init calls notify()
     *          mqtt_init();
     *          while (1) {
     *              publish();
     *              task.wait();    // normal DELAY interval or notify
     *          }
     */
    void sleep();

    /**
     * @brief Sleeps this task for ms milliseconds — watchdog-safe, can be woken early via notify().
     *        Schedules a one-shot timer that wakes this task, then calls sleep().
     *
     *        Typical usage — network stabilization before the first request:
     *          task.waitEvent(netEventGroup, NET_GOT_IP_BIT);
     *          task.notifyAfter(5000);  // waits 5s, watchdog-safe
     *          ota.check();
     *
     * @param ms Wait time in milliseconds
     */
    void notifyAfter(uint32_t ms);

    /**
     * @brief Block until all specified bits are set in an event group.
     *        Bits are NOT cleared on exit — race-condition-free: if bits are already
     *        set before the call, returns immediately. Watchdog is handled the same
     *        way as in wait() and sleep() — removed before blocking, re-added after.
     *
     *        Typical usage — wait for ethernet/wifi before MQTT init:
     *          task.waitEvent(ethEventGroup, ETH_GOT_IP_BIT);
     *          mqtt_init();
     *
     * @param group Event group handle (created by xEventGroupCreate())
     * @param bits  Bit mask to wait for (all bits must be set)
     */
    void waitEvent(EventGroupHandle_t group, EventBits_t bits);

    /**
     * @brief Block until the target task signals ready.
     *        Call at the start of a task function to express dependencies between tasks.
     *        Only the calling task is blocked — app_main and other tasks continue normally.
     *        Watchdog is handled the same way as in wait() and sleep() — removed before
     *        blocking, re-added after.
     *
     *        Example:
     *          task.waitReady("wifiTask");  // wait until wifiTask signals ready
     *
     * @param name      Name of the task to wait for
     * @param timeoutMs Maximum wait time in ms (default portMAX_DELAY = wait forever).
     *                  Still FreeRTOS-tick-based (xSemaphoreTake), unlike wait()'s
     *                  PERIODIC/DELAY timing — a one-shot startup-sync timeout is not
     *                  sensitive to tick-rate drift the way a repeating interval is,
     *                  so it wasn't worth the added complexity of an esp_timer-backed
     *                  semaphore wait here.
     * @return true if task signaled ready, false on timeout (logs warning)
     */
    bool waitReady(const char *name, uint32_t timeoutMs = portMAX_DELAY);

    /**
     * @brief Wait one interval/event before first loop iteration.
     *        Call before first wait(). Has no effect after initialization (logs warning).
     *        For EVENT mode: calls signalReady() automatically before blocking.
     */
    void waitBeforeStart();

    /**
     * @brief Wake this task — xTaskNotifyGive on own handle
     */
    void notify();

    /**
     * @brief Manually signal that the task is ready — unblocks all waitReady() callers.
     *        Called automatically on first wait(), or from waitBeforeStart() for EVENT mode.
     *        Use if you need to signal ready before the first wait() — e.g. after a long
     *        initialization to unblock dependent tasks before the loop starts.
     */
    void signalReady();

    /**
     * @brief Delete this task and remove from registry.
     *        Call from own task — no code runs after vTaskDelete().
     */
    void destroy();

    /**
     * @brief Suspend this task (vTaskSuspend)
     */
    void suspend();

    /**
     * @brief Resume this task (vTaskResume) — typically called from another task
     */
    void resume();

    // -------------------------------------------------------------------------
    //  Watchdog
    // -------------------------------------------------------------------------

    /** @brief Enable watchdog for this task */
    void enableWatchdog();

    /** @brief Disable watchdog for this task */
    void disableWatchdog();

    /** @brief Returns true if watchdog is enabled */
    bool isWatchdogEnabled() const;

    /**
     * @brief Manual watchdog reset.
     *        Use if you have a long operation inside the loop and want to keep watchdog protection.
     */
    void feedWatchdog();

    // -------------------------------------------------------------------------
    //  Configuration
    // -------------------------------------------------------------------------

    /**
     * @brief Set a new interval (takes effect on next wait())
     * @param intervalMs Interval in ms (min. 1)
     */
    void setInterval(uint32_t intervalMs);

    /** @brief Returns current interval in ms */
    uint32_t getInterval() const;

    /** @brief Returns last cycle run time in ms (time from last wait() return to this call) */
    uint32_t getLastRunTime() const;

    // -------------------------------------------------------------------------
    //  Timers
    // -------------------------------------------------------------------------

    /**
     * @brief Add a new timer independent of the task interval
     * @param intervalMs     Timer interval in ms
     * @param triggerOnStart true = fires immediately on first timer() call (default false)
     * @return Timer index to pass to timer()
     */
    int addTimer(uint32_t intervalMs, bool triggerOnStart = false);

    /**
     * @brief Check whether the timer has elapsed and automatically restart it
     * @param index Index from addTimer()
     * @return true if the timer elapsed in this cycle
     */
    bool timer(int index);

    // -------------------------------------------------------------------------
    //  Static — cross-task operations (by name from registry)
    // -------------------------------------------------------------------------

    /**
     * @brief Wake task by name (xTaskNotifyGive)
     * @return false if task was not found in registry (logs warning)
     */
    static bool notify(const char *name);

    /**
     * @brief Wake task by name after a delay — non-blocking one-shot timer.
     *        Combines with DELAY mode: task.wait() returns early when timer fires.
     *        Typical use: deferred wakeup after network connects (stack stabilization).
     *        The task name must remain valid until the timer fires — use string literals.
     *
     *        Example:
     *          task.waitEvent(netEventGroup, NET_GOT_IP_BIT);
     *          AP_TaskUtils::notifyAfter("otaTask", 5000); // wake after 5s
     *          // task.wait() in loop returns after 5s, not after full interval
     *
     * @param name  Task name in registry (string literal recommended)
     * @param ms    Delay in milliseconds
     */
    static void notifyAfter(const char *name, uint32_t ms);

    /**
     * @brief Delete task by name (vTaskDelete) and remove from registry
     * @return false if task was not found in registry (logs warning)
     */
    static bool destroy(const char *name);

    /**
     * @brief Suspend task by name (vTaskSuspend)
     * @return false if task was not found in registry (logs warning)
     */
    static bool suspend(const char *name);

    /**
     * @brief Resume task by name (vTaskResume)
     * @return false if task was not found in registry (logs warning)
     */
    static bool resume(const char *name);

    // -------------------------------------------------------------------------
    //  Static — utilities
    // -------------------------------------------------------------------------

    /**
     * @brief Initialize Task Watchdog Timer — call in app_main before tasks
     * @param timeoutMs Timeout in ms (default 5000)
     * @param panic     Trigger panic on timeout (default true)
     */
    static void initWatchdog(uint32_t timeoutMs = 5000, bool panic = true);

    /** @brief Time since boot in seconds */
    static uint64_t seconds();

    /** @brief Time since boot in milliseconds */
    static uint64_t millis();

    /** @brief Time since boot in microseconds */
    static uint64_t micros();

    /** @brief Blocking delay in ms (no watchdog reset) */
    static void delayMs(uint32_t ms);

    /** @brief Blocking delay in µs (no watchdog reset) */
    static void delayUs(uint32_t us);

    // -------------------------------------------------------------------------
    //  Static — global mutex
    // -------------------------------------------------------------------------

    /**
     * @brief No-op since v2.5.0 — the global mutex is now auto-initialized at
     *        static-init time, same as the internal registry mutex. Kept only for
     *        source compatibility with existing calls; no longer needs to be called.
     */
    static void initMutex();

    /**
     * @brief Lock global mutex (blocks until available)
     * @return true on success
     */
    static bool lock();

    /**
     * @brief Lock global mutex with timeout
     * @param timeoutMs Maximum wait time in ms
     * @return true on success
     */
    static bool lock(uint32_t timeoutMs);

    /** @brief Unlock global mutex */
    static void unlock();

private:
    struct TimerEntry {
        uint32_t intervalMs;
        uint64_t lastTrigger;
        bool     firstRun;
    };

    struct RegistryEntry {
        const char       *name;
        TaskHandle_t      handle;
        bool              ready;
        AP_TaskUtils     *self;      // owning instance — lets removal cancel its notifyAfter() timer
        int               busyCount; // in-flight notify/suspend/resume/destroy(name) ops pinning this handle
        bool              removing;  // true once removal has begun — blocks new pins (see _pinHandle)
        SemaphoreHandle_t drainSem;  // signaled when busyCount reaches 0 while removing
    };

    struct WaiterEntry {
        const char       *name;
        SemaphoreHandle_t sem;
        volatile bool    *targetDestroyed; // set true if the awaited task is destroyed before becoming ready
    };

    const char  *_tag;
    uint32_t     _intervalMs;
    Mode         _mode;
    bool         _useWatchdog;
    bool         _initialWaitDone;
    bool         _readySignaled;
    TaskHandle_t _taskHandle;
    int64_t      _lastWakeTimeUs;  // esp_timer microseconds — PERIODIC's previous-wake reference (was TickType_t pre-2.6.0)
    uint64_t     _cycleStart;
    uint32_t     _lastRunTime;
    esp_timer_handle_t _oneShot = nullptr;   // reusable one-shot timer — notifyAfter(ms) + internal PERIODIC/DELAY wait
    volatile bool       _timerFired = false; // set by _oneShot's callback; lets PERIODIC's wait ignore external notify()

    std::vector<TimerEntry> _timers;

    static SemaphoreHandle_t           _mutex;
    static StaticSemaphore_t           _mutexBuffer;
    static SemaphoreHandle_t           _registryMutex;  // protects _registry + _waiters (mutex, not spinlock → safe to allocate inside the lock)
    static StaticSemaphore_t           _registryMutexBuffer;
    static std::vector<RegistryEntry>  _registry;
    static std::vector<WaiterEntry>    _waiters;

    void                _registerTask();
    void                _blockForMode();

    // Lazily creates _oneShot with a shared callback: sets _timerFired, then
    // xTaskNotifyGive()s the owning task. Used by notifyAfter(ms) and _waitForTimer().
    // Caller must already hold a self-pin (_pinHandle(_tag)) — see both call sites.
    void                _ensureOneShot();

    // Arms _oneShot for durationUs and blocks on ulTaskNotifyTake(portMAX_DELAY).
    // ignoreExternalNotify=false (DELAY): any notify() satisfies the wait, same as
    // today. ignoreExternalNotify=true (PERIODIC): loops until _oneShot itself
    // fires, so an external notify() has no effect on timing — matches the old
    // vTaskDelayUntil behavior, which task notifications never interrupted.
    // Falls back to vTaskDelay() (tick-based) if esp_timer_create() fails (OOM) or
    // if a concurrent destroy(name)/destroy() has already claimed removal of this
    // task (self-pin fails) — see _pinHandle/_unpinHandle usage in the .cpp.
    void                _waitForTimer(int64_t durationUs, bool ignoreExternalNotify);

    // Pin/unpin: used by notify(name)/suspend(name)/resume(name) to hold a handle
    // stable against a concurrent self-destroy() while the FreeRTOS call is in
    // flight. Also self-pinned (_pinHandle(_tag)) by _waitForTimer()/notifyAfter(ms)
    // to hold _oneShot stable against a concurrent destroy(name)/destroy() targeting
    // this same task — same mechanism, same drain wait in _removeFromRegistry().
    static TaskHandle_t _pinHandle(const char *name);
    static void         _unpinHandle(const char *name);

    // Used by destroy() (self) and destroy(name) — waits out any in-flight pins,
    // then atomically cancels the owning instance's notifyAfter() timer, wakes
    // waitReady() waiters, and erases the entry. Returns the handle, or nullptr if
    // not found / already being removed by a concurrent call.
    static TaskHandle_t _removeFromRegistry(const char *name);

    const char         *_modeStr() const;
};
