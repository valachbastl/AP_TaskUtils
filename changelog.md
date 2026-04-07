# Changelog

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
