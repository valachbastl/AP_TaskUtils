# AP_TaskUtils

Utility library for FreeRTOS tasks in ESP-IDF. Simplifies task lifecycle management, watchdog handling, cross-task communication and shared data passing.

## Features

- **Three task modes** — `PERIODIC` (vTaskDelayUntil, precise interval), `DELAY` (ulTaskNotifyTake, wakeable anytime), `EVENT` (event-driven, no interval)
- **Task registry** — wake, suspend, resume or destroy any task by name without storing `TaskHandle_t`
- **No boilerplate** — no explicit `begin()` needed, ready signaled automatically on first `wait()`
- **`AP_Channel<T>`** — thread-safe shared state for 1 writer / N readers (ISR-safe)
- **`AP_Queue<T>`** — thread-safe event stream for N writers / 1 reader (ISR-safe)
- **Watchdog** — automatic handling during sleep, configurable per task at any time
- **Timers** — independent periodic timers per task, unlimited count
- **Global mutex** — for shared peripheral access (I2C, SPI, ...)
- **Time utilities** — `millis()`, `seconds()`, `micros()`, `delayMs()`, `delayUs()`

## Installation

```ini
lib_deps =
    https://github.com/valachbastl/AP_TaskUtils.git

# Or pinned to version:
    https://github.com/valachbastl/AP_TaskUtils.git#v2.0.0
```

## Quick Start

### app_main

```cpp
#include "AP_TaskUtils.h"

extern "C" void app_main(void)
{
    AP_TaskUtils::initWatchdog(5000);  // 5s timeout, panic on timeout
    AP_TaskUtils::initMutex();         // only if using lock()/unlock()

    sensor_task_create();
    mqtt_task_create();
    display_task_create();
}
```

### Recommended file pattern

Use `static const char *TAG` to share the task name across the entire file — for `AP_TaskUtils`, `ESP_LOGI` and `xTaskCreate`:

```cpp
// mqtt_task.cpp
#include "AP_TaskUtils.h"

static const char *TAG = "mqttTask";

static void mqttTask(void *pvParameters)
{
    AP_TaskUtils task(TAG, 60000, AP_TaskUtils::DELAY);

    while (1)
    {
        publishData();
        task.wait();  // sleeps up to 60s, can be woken early via AP_TaskUtils::notify(TAG)
    }
}

void mqtt_task_create()
{
    xTaskCreatePinnedToCore(mqttTask, TAG, 4096, NULL, 5, NULL, 1);
}
```

---

## Task Modes

### PERIODIC — precise interval (vTaskDelayUntil)

Automatically compensates for task run time. Use for sensors, control loops — anything that must run at exact intervals.

```cpp
static void sensorTask(void *pvParameters)
{
    AP_TaskUtils task(TAG, 1000);  // PERIODIC is default

    auto timerLog  = task.addTimer(10000);       // every 10s
    auto timerCalib = task.addTimer(3600000);    // every hour

    while (1)
    {
        readSensors();

        if (task.timer(timerLog))   logData();
        if (task.timer(timerCalib)) calibrate();

        task.wait();
    }
}
```

### DELAY — approximate interval, wakeable anytime (ulTaskNotifyTake)

Sleeps for up to `intervalMs` but can be woken immediately from another task. Use for MQTT publish, display refresh — tasks that run periodically but also on demand.

```cpp
static void mqttTask(void *pvParameters)
{
    AP_TaskUtils task(TAG, 60000, AP_TaskUtils::DELAY);

    while (1)
    {
        publishData();
        task.wait();  // woken by interval OR by AP_TaskUtils::notify(TAG)
    }
}

// From another task — trigger immediate publish:
AP_TaskUtils::notify("mqttTask");
```

### EVENT — event-driven, no interval (ulTaskNotifyTake forever)

Sleeps until explicitly woken. Use for error handlers, display updates, command processors.

```cpp
static void displayTask(void *pvParameters)
{
    AP_TaskUtils task(TAG, AP_TaskUtils::EVENT);  // waitBeforeStart=true is default for EVENT

    while (1)
    {
        updateDisplay();
        task.wait();  // sleeps until notify()
    }
}

// Wake display from sensor task:
AP_TaskUtils::notify("displayTask");
```

### waitReady — závislosti mezi tasky

`app_main` vytvoří všechny tasky a skončí. Každý task si sám počká na co potřebuje:

```cpp
// mqtt_task.cpp
static void mqttTask(void *pvParameters)
{
    AP_TaskUtils task(TAG, 60000, AP_TaskUtils::DELAY);

    AP_TaskUtils::waitReady("wifiTask");  // čeká než wifiTask signalizuje ready
                                          // app_main a ostatní tasky běží normálně

    initMqtt();

    while (1)
    {
        publishData();
        task.wait();
    }
}
```

---

## Shared Data

### AP_Channel\<T\> — shared state (1 writer, N readers)

Thread-safe latest-value store. Reader always gets the last written value without blocking.

```cpp
// interface.h
struct SensorData { float temp; float hum; };
extern AP_Channel<SensorData> sensorData;

// sensor_task.cpp — only this task writes
sensorData.set({.temp = 23.5f, .hum = 60.0f});

// display_task.cpp, mqtt_task.cpp — read anywhere
SensorData d = sensorData.get();
if (sensorData.isSet()) { ... }
```

### AP_Queue\<T\> — event stream (N writers, 1 reader)

Thread-safe queue. Use for commands, errors, events where every message must be processed.

```cpp
// interface.h
struct ErrorEvent { enum Source { MQTT, SENSOR, WIFI } source; bool failed; };
extern AP_Queue<ErrorEvent> errorQueue;

// mqtt_task.cpp, sensor_task.cpp — multiple writers
errorQueue.send({.source = ErrorEvent::MQTT, .failed = true});

// error_monitor_task.cpp — single reader
ErrorEvent e;
if (errorQueue.receive(e)) { ... }        // blocking
if (errorQueue.receive(e, 100)) { ... }   // with timeout
```

---

## API Reference

### AP_TaskUtils — constructors

| Constructor | Description |
|---|---|
| `AP_TaskUtils(tag, intervalMs, mode, watchdog, waitBeforeStart)` | PERIODIC / DELAY mód |
| `AP_TaskUtils(tag, mode, watchdog, waitBeforeStart)` | EVENT mód (no interval) |

Defaults: `mode = PERIODIC`, `watchdog = true`, `waitBeforeStart = false` (EVENT constructor: `waitBeforeStart = true`)

### AP_TaskUtils — instance methods

| Method | Description |
|---|---|
| `wait()` | Main blocking call — put at end of while(1) |
| `waitBeforeStart()` | Wait one interval/event before first loop body run — call before first `wait()` |
| `signalReady()` | Manually signal ready — unblocks all `waitReady()` callers; called automatically on first `wait()` or from `waitBeforeStart()` for EVENT mode |
| `notify()` | Wake this task |
| `destroy()` | Delete this task and remove from registry |
| `suspend()` | Suspend this task |
| `resume()` | Resume this task |
| `enableWatchdog()` / `disableWatchdog()` | Enable/disable watchdog at runtime |
| `isWatchdogEnabled()` | Check watchdog state |
| `feedWatchdog()` | Manual watchdog reset (for long operations inside loop) |
| `setInterval(ms)` | Change interval (takes effect on next `wait()`) |
| `getInterval()` | Get current interval |
| `getLastRunTime()` | Get last cycle run time in ms |
| `addTimer(intervalMs, triggerOnStart)` | Add independent periodic timer, returns index |
| `timer(index)` | Returns true when timer elapsed (auto-restarts) |

### AP_TaskUtils — static cross-task operations

| Method | Description |
|---|---|
| `notify(name)` | Wake task by name |
| `destroy(name)` | Delete task by name |
| `suspend(name)` | Suspend task by name |
| `resume(name)` | Resume task by name |
| `waitReady(name, timeoutMs)` | Block until task signals ready (first `wait()` or manual `signalReady()`), default timeout portMAX_DELAY |

### AP_TaskUtils — static utilities

| Method | Description |
|---|---|
| `initWatchdog(timeoutMs, panic)` | Initialize WDT — call in app_main before tasks |
| `millis()` | Time since boot in ms |
| `seconds()` | Time since boot in s |
| `micros()` | Time since boot in µs |
| `delayMs(ms)` | Blocking delay in ms (no watchdog reset) |
| `delayUs(us)` | Blocking delay in µs (no watchdog reset) |
| `initMutex()` | Initialize global mutex — call in app_main |
| `lock()` | Lock global mutex (blocks until available) |
| `lock(timeoutMs)` | Lock with timeout |
| `unlock()` | Unlock global mutex |

### AP_Channel\<T\>

| Method | Description |
|---|---|
| `set(value)` | Write value (overwrites previous, never blocks) |
| `setFromISR(value)` | Write from ISR context |
| `get()` | Read latest value (non-blocking, returns `T{}` if never set) |
| `isSet()` | Returns true if value was written at least once |

### AP_Queue\<T\>

| Method | Description |
|---|---|
| `send(item)` | Send item (blocks until space available) |
| `send(item, timeoutMs)` | Send with timeout (0 = non-blocking) |
| `sendFromISR(item)` | Send from ISR context |
| `receive(item)` | Receive item (blocks until item available) |
| `receive(item, timeoutMs)` | Receive with timeout (0 = non-blocking) |
| `available()` | Number of items waiting in queue |
| `clear()` | Flush queue |

## Author

Petr Adámek
