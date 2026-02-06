# Changelog

## [1.2.1] - 2026-02-06

### Added
- Metody `enableCompensation()`, `disableCompensation()`, `isCompensationEnabled()` pro volbu zda pouzit kompenzaci doby behu
- Kompenzace je ve vychozim stavu zapnuta (zpetne kompatibilni s v1.2.0)

## [1.2.0] - 2026-02-06

### Changed
- Metoda `delay()` nyni kompenzuje dobu behu tasku - automaticky dopocita zbytkovy cas tak, aby celkovy cyklus odpovidal nastavenemu intervalu
- Pokud task pretahne interval, delay je minimalne 1ms (task se nezablokuje)

### Added
- Nova metoda `getLastRunTime()` vraci dobu behu posledniho cyklu v ms (pro diagnostiku)
- Metoda `begin(bool startImmediately)` - volitelne zpozdeni startu tasku o jeden interval

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
