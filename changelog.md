# Changelog

## [2.4.0] - 2026-06-11

### Fixed
- `destroy(name)` (static) nyni odhlasi cilovy task z Task Watchdogu (`esp_task_wdt_delete`) pred `vTaskDelete`. Drive smazani tasku se zapnutym watchdogem z jineho tasku nechalo task registrovany v TWDT → watchdog panic. Instancni `destroy()` to uz osetreno mel.
- `signalReady()` uz neomezuje pocet notifikovanych `waitReady()` cekatelu na 8 (pevny buffer) — 9. a dalsi cekatel na stejny task mohl viset. Nyni se sbiraji do dynamickeho vektoru.

### Changed
- Zamek registry/waiters prepnut z `portMUX_TYPE` spinlocku na FreeRTOS mutex (`_registryMutex`, vytvoreny pri static-init — zadny init call, transparentni). Alokace (`std::vector` rust, vytvoreni semaforu) uvnitr zamku je nyni legalni dle pravidel ESP-IDF; spinlock ji zakazoval. Dual-core vzajemne vylouceni registry (oprava z 2.2.0) zustava zachovano. Registry operace podle jmena uz nejsou volatelne z ISR (z ISR se nikdy nepouzivaly).
- Licence zmenena na MIT (drive UNLICENSED).

### Added
- `waitReady()` kontroluje navratovou hodnotu `xSemaphoreCreateBinary()` (pri selhani alokace vrati false misto pristupu na null).
- `AP_Channel<T>` / `AP_Queue<T>` metody osetruji null queue (selhani `xQueueCreate` pri OOM) — degraduji gracefully (no-op / false / `T{}` / 0) misto pristupu na null.

## [2.3.0] - 2026-06-01

### Added
- `notifyAfter(ms)` — instance method; watchdog-safe sleep for `ms` ms via a one-shot `esp_timer` + `sleep()`. Wakeable earlier by `notify()`.
- `notifyAfter(name, ms)` — static method; wakes the named task after `ms` ms without blocking the caller. Deferred counterpart to `notify(name)`.

## [2.2.0] - 2026-04-08

### Added
- `waitEvent(group, bits)` — instance method; blocks the calling task until all specified bits are set in an EventGroup. Bits are NOT cleared on exit (pdFALSE), so the call is race-condition-free: if bits are already set before the call, it returns immediately. Watchdog is handled identically to `sleep()` — removed before blocking, re-added on wake. Typical use: wait for network link-up signaled by an `EventGroupHandle_t` (set in an ISR or event handler) before initializing MQTT or other network-dependent services.

### Changed
- `waitReady(name, timeoutMs)` promoted from static to instance method — now handles watchdog identically to `sleep()` and `waitEvent()` (removed before blocking, re-added on wake), and correctly updates `_lastRunTime` / `_cycleStart` so `getLastRunTime()` remains accurate after the wait. Call site changes from `AP_TaskUtils::waitReady(...)` to `task.waitReady(...)`.

### Fixed
- Thread-safety of internal task registry (`_registry`, `_waiters`) on dual-core ESP32. Concurrent calls to `_registerTask()` from tasks running on different cores could corrupt the `std::vector` internal state during reallocation (heap corruption, TLSF assert). Fixed by protecting all registry and waiters access with a `portMUX_TYPE` spinlock (`_registryMux`) that is statically initialized — no user action required, fully transparent.
- `waitReady()` no longer allocates heap (semaphore creation, `_waiters.push_back`) inside the critical section — semaphore is created before acquiring the spinlock, with a double-check for the ready state after re-acquiring the lock to close the race window.

## [2.1.0] - 2026-04-08

### Added
- `sleep()` — blocks the task until the next `notify()`, regardless of mode and interval. Equivalent to EVENT-mode wait but callable from any mode. Watchdog is automatically removed before blocking and re-added after waking. For PERIODIC mode, `_lastWakeTime` is reset on wake so the next `wait()` starts a clean interval. Typical use: wait for a one-time event (e.g. IP address obtained) before entering the main loop.

## [2.0.0] - 2026-04-07

Complete rewrite. New API — not backward compatible with v1.x.

### Breaking Changes
- Constructor signature changed — interval and mode are now separate parameters
- `NO_PERIOD` mode renamed to `EVENT`
- `begin()` removed — initialization happens automatically in the constructor
- Task registry, `AP_Channel<T>`, `AP_Queue<T>` and `waitReady()` are new concepts with no v1.x equivalent

### Added
- Three explicit task modes: `PERIODIC` (vTaskDelayUntil), `DELAY` (ulTaskNotifyTake + timeout), `EVENT` (ulTaskNotifyTake without timeout)
- Second constructor for EVENT mode without interval parameter
- `waitBeforeStart()` — method and constructor parameter; waits one interval/event before first loop iteration; for EVENT mode defaults to `true`
- Task registry — each task registers itself in the constructor by name (std::vector, no limit)
- `AP_TaskUtils::notify(name)` — wake task by name without storing TaskHandle_t
- `AP_TaskUtils::destroy(name)` — delete task by name and remove from registry
- `AP_TaskUtils::suspend(name)` / `resume(name)` — suspend/resume task by name
- Instance methods `notify()`, `destroy()`, `suspend()`, `resume()` operating on own handle
- `AP_Channel<T>` — shared state for 1 writer and N readers (xQueueOverwrite, thread-safe, ISR-safe)
- `AP_Queue<T>` — event stream for N writers and 1 reader (FreeRTOS queue wrapper, ISR-safe)
- New files `AP_Channel.h` and `AP_Queue.h` — can be included standalone or via `AP_TaskUtils.h`
- PERIODIC mode uses `vTaskDelayUntil` — precise interval with automatic run time compensation
- DELAY mode uses `ulTaskNotifyTake` with timeout — approximate interval, wakeable anytime
- EVENT mode uses `ulTaskNotifyTake` without timeout — purely event-driven, woken only via `notify()`
- Watchdog configurable in constructor and at runtime (`enableWatchdog()` / `disableWatchdog()`)
- Timers (`addTimer()` / `timer()`) — unlimited count, independent of task interval
- `AP_TaskUtils::waitReady(name, timeoutMs)` — blocks calling task until target task signals ready; allows expressing dependencies between tasks without blocking app_main
- `signalReady()` — signals ready; called automatically on first `wait()`, or from `waitBeforeStart()` for EVENT mode before blocking; can also be called manually
- Global mutex (`initMutex()` / `lock()` / `unlock()`) for simple shared resource access
- Static utilities: `millis()`, `seconds()`, `micros()`, `delayMs()`, `delayUs()`, `initWatchdog()`
