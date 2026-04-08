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
        PERIODIC,  // vTaskDelayUntil     — precise interval, automatic run time compensation
        DELAY,     // ulTaskNotifyTake    — approximate interval, wakeable anytime
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
     *        PERIODIC → vTaskDelayUntil (precise interval, compensates run time)
     *        DELAY    → ulTaskNotifyTake with timeout (wakeable anytime)
     *        EVENT    → ulTaskNotifyTake without timeout (woken exclusively by notify())
     */
    void wait();

    /**
     * @brief Zablokuje task až do příchodu notify() — bez timeoutu, bez ohledu na mód.
     *        Funguje kdekoliv v těle tasku (před smyčkou i uvnitř while(1)).
     *        Watchdog se chová stejně jako v wait() — task je dočasně odhlášen.
     *        Po probuzení se PERIODIC timer resetuje, takže wait() pokračuje správně.
     *
     *        Typické použití — čekání na síťovou událost před prvním cyklem:
     *          task.sleep();       // spí dokud ethernet_init nezavolá notify()
     *          mqtt_init();
     *          while (1) {
     *              publish();
     *              task.wait();    // normální DELAY interval nebo notify
     *          }
     */
    void sleep();

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
     * @param timeoutMs Maximum wait time in ms (default portMAX_DELAY = wait forever)
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

    /** @brief Initialize global mutex — call in app_main before tasks */
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
        const char  *name;
        TaskHandle_t handle;
        bool         ready;
    };

    struct WaiterEntry {
        const char       *name;
        SemaphoreHandle_t sem;
    };

    const char  *_tag;
    uint32_t     _intervalMs;
    Mode         _mode;
    bool         _useWatchdog;
    bool         _initialWaitDone;
    bool         _readySignaled;
    TaskHandle_t _taskHandle;
    TickType_t   _lastWakeTime;
    uint64_t     _cycleStart;
    uint32_t     _lastRunTime;

    std::vector<TimerEntry> _timers;

    static SemaphoreHandle_t           _mutex;
    static portMUX_TYPE                _registryMux;
    static std::vector<RegistryEntry>  _registry;
    static std::vector<WaiterEntry>    _waiters;

    void                _registerTask();
    static void         _unregisterByHandle(TaskHandle_t handle);
    static TaskHandle_t _findHandle(const char *name);
    const char         *_modeStr() const;
};
