# AP_TaskUtils

Utility library for FreeRTOS tasks in ESP-IDF.

## Features

- Task initialization with logging and watchdog registration
- Automatic watchdog handling during task sleep
- **Execution time compensation** - delay automatically adjusts to maintain consistent cycle intervals
- Optional watchdog disable per task
- Global mutex for shared data access
- Arduino-like `millis()` and `micros()` functions
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
    https://github.com/valachbastl/AP_TaskUtils.git#v1.2.1
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
uint64_t time_ms = AP_TaskUtils::millis();
uint64_t time_us = AP_TaskUtils::micros();

AP_TaskUtils::delayMs(100);  // without watchdog handling
AP_TaskUtils::delayUs(50);
```

## API Reference

### Instance Methods

| Method | Description |
|--------|-------------|
| `AP_TaskUtils(tag, delayMs, useWatchdog)` | Constructor (useWatchdog default true) |
| `begin(startImmediately)` | Initialize task (default true, false = wait one interval before start) |
| `delay()` | Sleep with watchdog handling and run time compensation |
| `getLastRunTime()` | Get last cycle run time in ms |
| `setDelay(ms)` | Change delay interval |
| `getDelay()` | Get current delay |
| `feedWatchdog()` | Manual watchdog reset |
| `enableWatchdog()` | Enable watchdog at runtime |
| `disableWatchdog()` | Disable watchdog at runtime |
| `isWatchdogEnabled()` | Check if watchdog is enabled |
| `enableCompensation()` | Enable run time compensation (default) |
| `disableCompensation()` | Disable run time compensation (plain vTaskDelay) |
| `isCompensationEnabled()` | Check if compensation is enabled |

### Static Methods

| Method | Description |
|--------|-------------|
| `initWatchdog(timeoutMs, panic)` | Initialize watchdog timer |
| `initMutex()` | Initialize global mutex |
| `lock()` | Lock mutex (waits forever) |
| `lock(timeoutMs)` | Lock mutex with timeout |
| `unlock()` | Unlock mutex |
| `millis()` | Time since boot in ms |
| `micros()` | Time since boot in us |
| `delayMs(ms)` | Delay in milliseconds |
| `delayUs(us)` | Delay in microseconds |

## Author

Petr Adámek
