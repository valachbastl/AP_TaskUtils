# Changelog

## [1.1.0] - 2026-01-30

### Added
- Moznost vypnout watchdog pro task v konstruktoru `AP_TaskUtils(tag, delayMs, useWatchdog)`
- Runtime metody `enableWatchdog()`, `disableWatchdog()`, `isWatchdogEnabled()`
- Logovani stavu watchdogu pri startu tasku

### Changed
- Metody `begin()`, `delay()`, `feedWatchdog()` nyni respektuji nastaveni watchdogu

## [1.0.0] - 2026-01-27

### Added
- Inicializace watchdogu `initWatchdog(timeoutMs, panic)`
- Task helper metody `begin()`, `delay()`, `setDelay()`, `getDelay()`
- Manualni watchdog reset `feedWatchdog()`
- Casove funkce `millis()`, `micros()`
- Staticke delay funkce `delayMs()`, `delayUs()`
- Automaticke odhlaseni z watchdogu behem spanku tasku
- Globalni mutex `initMutex()`, `lock()`, `lock(timeoutMs)`, `unlock()`
