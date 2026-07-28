# Changelog

## [2.6.0] - 2026-07-28

### Fixed
- `PERIODIC` and `DELAY` mode timing switched from the FreeRTOS tick (`vTaskDelayUntil`
  / `ulTaskNotifyTake` with a tick-based timeout) to the monotonic `esp_timer` (one-shot
  HW timer + untimed `ulTaskNotifyTake`). On ESP-IDF with dynamic frequency scaling
  (`esp_pm`, DFS), the FreeRTOS tick can run measurably faster than real time — a known,
  unresolved ESP-IDF/FreeRTOS limitation (`CONFIG_FREERTOS_TICK_SUPPORT_CORETIMER` ties
  the tick to the CPU cycle counter, which DFS frequency switches can decalibrate; see
  espressif/esp-idf#17992). Measured ~3% drift per minute on real HW with DFS enabled,
  reproducible both on the bench and in the field (an OTA client using this library's
  `DELAY` mode skipped every other check-in). `esp_timer` is unaffected by DFS, so timing
  is now accurate regardless of whether DFS is active.
  - `PERIODIC` keeps the exact same catch-up semantics as `vTaskDelayUntil` (advances the
    wake reference by exactly one interval per cycle, never bursts through multiple missed
    periods) and remains uninterruptible by `notify()`, same as before.
  - `DELAY` keeps its "wakeable anytime via notify()" behavior.
  - `waitReady(name, timeoutMs)`'s timeout is intentionally left tick-based — a one-shot
    startup-sync wait isn't sensitive to tick-rate drift the way a repeating interval is,
    and an `esp_timer`-backed semaphore wait would add real complexity for no practical gain.
  - No public API changes.
- Closed a use-after-free introduced by the `esp_timer` switch above: every `wait()` cycle
  in `PERIODIC`/`DELAY` mode now touches the per-instance `_oneShot` timer, but nothing
  protected it against a concurrent `destroy(name)`/`destroy()` (from another task)
  deleting that same timer mid-arm. `_waitForTimer()` and `notifyAfter(ms)` now self-pin
  (`_pinHandle(_tag)`/`_unpinHandle(_tag)`) around every `_oneShot` create/arm/stop —
  the same busyCount/drain mechanism `_removeFromRegistry()` already used for
  `notify(name)`/`suspend(name)`/`resume(name)`. The pin is held only for the short
  create-and-arm sequence, released before the actual blocking wait, so `destroy(name)`
  is never held up for a full interval.
- `AP_TaskUtils::notifyAfter(name, ms)` (static) now checks `esp_timer_start_once()`'s
  return value — on failure it now deletes the timer and frees its context instead of
  leaking both permanently.
- `AP_Channel<T>`/`AP_Queue<T>` now `static_assert(std::is_trivially_copyable<T>::value)`.
  FreeRTOS queues copy `T` byte-for-byte; a non-trivially-copyable `T` (owning pointers,
  `std::string`/`std::vector`, a user-defined copy ctor/dtor) used to compile silently
  and corrupt/double-free at runtime instead of failing at compile time.

## [2.5.0] - 2026-07-27

Vyresene nalezy z plneho auditu 2026-07-08 (viz TODO_next_version.md). Beze
zmeny verejneho API pro existujici volajici.

### Fixed
- `waitEvent()` a `waitReady()` nyni restartuji `_lastWakeTime` stejne jako
  `sleep()` - PERIODIC task po dlouhem cekani v nich uz "nedohani" zameskane
  cykly `vTaskDelayUntil` najednou.
- 2-parametrovy konstruktor `(tag, mode)` clampuje `_intervalMs` na min. 1
  (drive 0) - pri omylem pouzitem `DELAY` uz nemuze dojit k busy-spinu.
- `signalReady()` davala `xSemaphoreGive()` na semafor cekatele AZ PO
  uvolneni `_registryMutex` - pokud mezitim `waitReady()` s timeoutem vyprsel
  a semafor smazal, slo o use-after-free. Give je nyni ve stejne kriticke
  sekci jako scan `_waiters`.
- Staticke `destroy(name)` nyni zrusi i instance vlastni `notifyAfter()`
  timer (drive zustal zit po smazani tasku a po vystrelu volal
  `xTaskNotifyGive()` na uvolneny/znovupouzity handle).
- `waitReady()` uz nevisi navzdy (ani do timeoutu), kdyz je cekany task
  smazan drive, nez stihne `signalReady()` - vraci `false`.
- Opravena TOCTOU race v `notify(name)` / `suspend(name)` / `resume(name)` /
  `destroy(name)`: mezi nalezenim handle v registru a jeho pouzitim mohl
  cilovy task mezitim sam zavolat vlastni `destroy()` (self-delete) a operace
  pak pracovala s uvolnenym/znovupouzitym handle. Kazdy registry zaznam ma
  nyni pocitadlo probihajicich name-based operaci; self-destroy (i
  `destroy(name)`) pockaji, az klesne na 0, nez handle skutecne zaniknou.

### Changed
- `_mutex` (globalni zamek pro `lock()`/`unlock()`) i `_registryMutex` jsou
  nyni staticky alokovane (`xSemaphoreCreateMutexStatic` +
  `StaticSemaphore_t`) a vytvoreji se rovnou pri static-init, stejne jako
  drive jen `_registryMutex` - nemuze selhat kvuli OOM heapu a `initMutex()`
  uz neni potreba volat. `initMutex()` zustava jako no-op kvuli zdrojove
  kompatibilite se stavajicim kodem.
- `wait()` a `waitBeforeStart()` sdileji spolecny mode-switch blok
  (`_blockForMode()`) misto dvou kopii.
- `AP_Channel<T>` a `AP_Queue<T>` sdileji spolecnou internal bazi
  (`AP_QueueBase<T>`) pro `_queue` handle, konstrukci/destrukci a zakaz
  kopirovani - drive duplicitni v obou souborech.

## [2.4.1] - 2026-06-16

### Added
- ESP-IDF component baleni (`CMakeLists.txt` + `idf_component.yml`) - pouzitelne jako cista ESP-IDF komponenta, nejen pres PlatformIO. Bez zmeny kodu.

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
