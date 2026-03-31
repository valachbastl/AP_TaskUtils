# Changelog

## [1.5.1] - 2026-03-31

### Fixed
- README: NO_PERIOD priklad opraven - `begin(false)` a `task.delay()` na konci smycky (spravny vzor pro event-driven task)
- README: `begin()` API tabulka - doplnena poznamka ze pro `NO_PERIOD` je doporucen `begin(false)`
- Log zprava v `disableNotifyDelay()` prelozena do anglictiny (konzistentni s ostatnimi log zpravami)

## [1.5.0] - 2026-03-30

### Added
- Konstanta `NO_PERIOD` (`UINT32_MAX`) pro tasky bez periodickeho volani - task ceka v `delay()` na `xTaskNotifyGive()` s `portMAX_DELAY`
- `setDelay(NO_PERIOD)` automaticky zapne `enableNotifyDelay()`
- `begin(false)` s `NO_PERIOD` ceka na prvni `xTaskNotifyGive()` pred prvnim pruchodem

### Changed
- Konstruktor nyni vola `setDelay()` interně - logika inicializace na jednom miste
- `disableNotifyDelay()` ignoruje volani pokud je nastaven `NO_PERIOD` (s varovanim v logu)

## [1.4.0] - 2026-02-16

### Added
- Timer system `addTimer(intervalMs, triggerOnStart)`, `timer(index)` - periodicky spousteny timer nezavisly na delay intervalu tasku
- Libovolny pocet timeru na task (dynamicky pres `std::vector`)
- Volitelny `triggerOnStart` parametr - spusti timer hned pri prvnim volani `timer()` (default false)

## [1.3.0] - 2026-02-13

### Added
- Notify delay rezim `enableNotifyDelay()`, `disableNotifyDelay()`, `isNotifyDelayEnabled()` - delay() pouzije `ulTaskNotifyTake` misto `vTaskDelay`, coz umozni okamzite probuzeni tasku pres `xTaskNotifyGive`
- Staticka metoda `seconds()` - cas od startu v sekundach

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
