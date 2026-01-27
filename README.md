# AP_TaskUtils

Utility library for FreeRTOS tasks in ESP-IDF.

## Features

- Task initialization with logging and watchdog registration
- Automatic watchdog handling during task sleep
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
    https://github.com/valachbastl/AP_TaskUtils.git#v1.0.0
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
    AP_TaskUtils task("myTask", 100);  // tag, delay in ms
    task.begin();

    while (1)
    {
        // Access shared data
        AP_TaskUtils::lock();
        // ... work with shared variables ...
        AP_TaskUtils::unlock();

        // Your task code here

        task.delay();  // handles watchdog automatically
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
| `AP_TaskUtils(tag, delayMs)` | Constructor |
| `begin()` | Initialize task (log + watchdog) |
| `delay()` | Sleep with watchdog handling |
| `setDelay(ms)` | Change delay interval |
| `getDelay()` | Get current delay |
| `feedWatchdog()` | Manual watchdog reset |

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
