# Changelog

## [1.0.0] - 2026-01-27

### Added
- Inicializace watchdogu `initWatchdog(timeoutMs, panic)`
- Task helper metody `begin()`, `delay()`, `setDelay()`, `getDelay()`
- Manualni watchdog reset `feedWatchdog()`
- Casove funkce `millis()`, `micros()`
- Staticke delay funkce `delayMs()`, `delayUs()`
- Automaticke odhlaseni z watchdogu behem spanku tasku
- Globalni mutex `initMutex()`, `lock()`, `lock(timeoutMs)`, `unlock()`
