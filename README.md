# AP_TaskUtils

Utility library for FreeRTOS tasks in ESP-IDF.

## Features

- Task initialization with logging and watchdog registration
- Automatic watchdog handling during task sleep
- **Execution time compensation** - delay automatically adjusts to maintain consistent cycle intervals
- **Notify delay mode** - task can be woken up immediately via `xTaskNotifyGive` (useful for on-demand triggers)
- **`NO_PERIOD` mode** - task with no periodic interval, sleeps forever until woken via `xTaskNotifyGive` (ideal for event-driven tasks like TTS, MQTT publish on demand)
- **Timer system** - periodic timers independent of task delay interval, with optional trigger on start
- Optional watchdog disable per task
- Global mutex for shared data access
- Time functions `seconds()`, `millis()`, `micros()`
- Static delay functions

## Installation

### PlatformIO

Add to `platformio.ini`:

```ini
lib_deps =
    https://github.com/valachbastl/AP_TaskUtils.git
```

Or with specific version:

```ini
lib_deps =
    https://github.com/valachbastl/AP_TaskUtils.git#v1.5.1
```

## Usage

### Initialization (app_main)

```cpp
#include "AP_TaskUtils.h"

extern "C" void app_main(void)
{
    AP_TaskUtils::initWatchdog(5000, true);  // 5s timeout, panic on timeout
    AP_TaskUtils::initMutex();

    // Create tasks...
}
```

### Task Example

```cpp
#include "AP_TaskUtils.h"

void myTask(void *pvParameters)
{
    AP_TaskUtils task("myTask", 100);  // tag, 100ms cycle interval
    task.begin();

    while (1)
    {
        // Access shared data
        AP_TaskUtils::lock();
        // ... work with shared variables ...
        AP_TaskUtils::unlock();

        // Your task code here
        // e.g. if this takes 30ms, delay() will sleep only 70ms
        // to maintain consistent 100ms cycle

        task.delay();  // compensates for task run time automatically
    }
}
```

### Monitoring Task Run Time

```cpp
void sensorTask(void *pvParameters)
{
    AP_TaskUtils task("sensorTask", 200);  // 200ms cycle
    task.begin();

    while (1)
    {
        readSensors();
        processData();

        // Check if task is close to overrunning its interval
        if (task.getLastRunTime() > task.getDelay() * 0.8) {
            ESP_LOGW("sensor", "Task taking too long: %lu ms", task.getLastRunTime());
        }

        task.delay();
    }
}
```

### Delayed Start

```cpp
void secondaryTask(void *pvParameters)
{
    AP_TaskUtils task("secondary", 100);
    task.begin(false);  // waits 100ms before first execution

    while (1)
    {
        // task code
        task.delay();
    }
}
```

### Task Without Compensation

```cpp
void lvglTask(void *pvParameters)
{
    AP_TaskUtils task("taskLVGL", 10);
    task.disableCompensation();  // plain vTaskDelay
    task.begin();

    while (1)
    {
        lv_timer_handler();
        task.delay();  // always waits full 10ms
    }
}
```

### Notify Delay (on-demand wakeup)

```cpp
static TaskHandle_t taskHandle = NULL;

void mqttTask(void *pvParameters)
{
    AP_TaskUtils task("mqttTask", 60000);  // 60s interval
    task.enableNotifyDelay();
    task.begin();

    while (1)
    {
        publishData();
        task.delay();  // sleeps up to 60s, wakes on xTaskNotifyGive(taskHandle)
    }
}

// Call from another task or event handler to trigger immediate wakeup
void requestUpdate(void)
{
    if (taskHandle) xTaskNotifyGive(taskHandle);
}
```

### NO_PERIOD — event-driven task (no periodic interval)

```cpp
static TaskHandle_t ttsHandle = NULL;

void ttsTask(void *pvParameters)
{
    AP_TaskUtils task("ttsTask", AP_TaskUtils::NO_PERIOD);  // no periodic interval, enableNotifyDelay set automatically
    task.begin(false);  // wait for first xTaskNotifyGive before entering loop

    while (1)
    {
        // process event (e.g. read from queue and synthesize speech)
        processEvent();

        task.delay();   // sleeps forever until next xTaskNotifyGive(ttsHandle)
    }
}

// Wake up the task from another context
void triggerTTS(void)
{
    if (ttsHandle) xTaskNotifyGive(ttsHandle);
}
```

### Periodic Timers

```cpp
void sensorTask(void *pvParameters)
{
    AP_TaskUtils task("sensorTask", 1000);  // 1s cycle
    int8_t timerCalibrate = task.addTimer(3600000);        // every hour
    int8_t timerLog = task.addTimer(10000, true);          // every 10s, runs immediately on start
    task.begin();

    while (1)
    {
        if (task.timer(timerCalibrate)) {
            // runs once per hour
            calibrateSensor();
        }

        if (task.timer(timerLog)) {
            // runs every 10s (and on first iteration)
            logData();
        }

        readSensor();
        task.delay();
    }
}
```

### Task Without Watchdog

```cpp
void backgroundTask(void *pvParameters)
{
    AP_TaskUtils task("bgTask", 1000, false);  // watchdog disabled
    task.begin();

    while (1)
    {
        // Long running operations without watchdog timeout
        task.delay();
    }
}
```

### Static Utilities

```cpp
uint64_t time_s  = AP_TaskUtils::seconds();
uint64_t time_ms = AP_TaskUtils::millis();
uint64_t time_us = AP_TaskUtils::micros();

AP_TaskUtils::delayMs(100);  // without watchdog handling
AP_TaskUtils::delayUs(50);
```

## API Reference

### Instance Methods

| Method | Description |
|--------|-------------|
| `AP_TaskUtils(tag, delayMs, useWatchdog)` | Constructor (useWatchdog default true, delayMs = `NO_PERIOD` for event-driven task) |
| `begin(startImmediately)` | Initialize task (default true, false = wait one interval / one notify before start — recommended for `NO_PERIOD`) |
| `delay()` | Sleep with watchdog handling and run time compensation |
| `getLastRunTime()` | Get last cycle run time in ms |
| `setDelay(ms)` | Change delay interval (`NO_PERIOD` = event-driven, enables notify delay automatically) |
| `getDelay()` | Get current delay |
| `feedWatchdog()` | Manual watchdog reset |
| `enableWatchdog()` | Enable watchdog at runtime |
| `disableWatchdog()` | Disable watchdog at runtime |
| `isWatchdogEnabled()` | Check if watchdog is enabled |
| `enableCompensation()` | Enable run time compensation (default) |
| `disableCompensation()` | Disable run time compensation (plain vTaskDelay) |
| `isCompensationEnabled()` | Check if compensation is enabled |
| `enableNotifyDelay()` | Enable notify delay mode (wakeup via `xTaskNotifyGive`) |
| `disableNotifyDelay()` | Disable notify delay mode (back to `vTaskDelay`, ignored for `NO_PERIOD` tasks) |
| `isNotifyDelayEnabled()` | Check if notify delay is enabled |
| `addTimer(intervalMs, triggerOnStart)` | Create periodic timer, returns index (triggerOnStart default false) |
| `timer(index)` | Check if timer elapsed, auto-restarts (returns true when fired) |

### Static Methods

| Method | Description |
|--------|-------------|
| `initWatchdog(timeoutMs, panic)` | Initialize watchdog timer |
| `initMutex()` | Initialize global mutex |
| `lock()` | Lock mutex (waits forever) |
| `lock(timeoutMs)` | Lock mutex with timeout |
| `unlock()` | Unlock mutex |
| `seconds()` | Time since boot in s |
| `millis()` | Time since boot in ms |
| `micros()` | Time since boot in us |
| `delayMs(ms)` | Delay in milliseconds |
| `delayUs(us)` | Delay in microseconds |

## Author

Petr Adámek
