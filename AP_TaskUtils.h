#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_log.h"

class AP_TaskUtils
{
public:
    /**
     * @brief Konstruktor
     * @param tag Nazev tasku pro logovani
     * @param delayMs Vychozi delay v milisekundach (default 10ms)
     * @param useWatchdog Povolit automaticky watchdog (default true)
     */
    AP_TaskUtils(const char *tag, uint32_t delayMs = 10, bool useWatchdog = true);

    /**
     * @brief Inicializace tasku - zaloguje start a prihasi k watchdogu
     *        Volat na zacatku task funkce
     * @param startImmediately true = task zacne okamzite, false = pocka jeden interval pred startem (default true)
     */
    void begin(bool startImmediately = true);

    /**
     * @brief Delay s watchdog resetem a kompenzaci doby behu tasku
     *        Volat na konci while(1) smycky
     *        Automaticky dopocita zbytkovy cas tak, aby celkovy cyklus
     *        odpovidal nastavenemu intervalu
     */
    void delay();

    /**
     * @brief Vrati dobu behu posledniho cyklu v milisekundach
     *        (cas mezi begin()/delay() a nasledujicim delay())
     * @return Doba behu v ms
     */
    uint32_t getLastRunTime();

    /**
     * @brief Nastavi novy delay interval
     * @param delayMs Delay v milisekundach
     */
    void setDelay(uint32_t delayMs);

    /**
     * @brief Vrati aktualni delay interval
     * @return Delay v milisekundach
     */
    uint32_t getDelay();

    /**
     * @brief Reset watchdogu (pokud potrebujete volat manualne)
     */
    void feedWatchdog();

    /**
     * @brief Zapne watchdog pro tento task
     *        Prida task k WDT pokud jeste neni pridan
     */
    void enableWatchdog();

    /**
     * @brief Vypne watchdog pro tento task
     *        Odebere task z WDT
     */
    void disableWatchdog();

    /**
     * @brief Zjisti zda je watchdog povolen
     * @return true pokud je watchdog aktivni
     */
    bool isWatchdogEnabled();

    /**
     * @brief Zapne kompenzaci doby behu (default)
     *        delay() bude dopocitavat zbytkovy cas
     */
    void enableCompensation();

    /**
     * @brief Vypne kompenzaci doby behu
     *        delay() bude vzdy cekat plny interval
     */
    void disableCompensation();

    /**
     * @brief Zjisti zda je kompenzace povolena
     * @return true pokud je kompenzace aktivni
     */
    bool isCompensationEnabled();

    // --- Staticke utility funkce ---

    /**
     * @brief Inicializace Task Watchdog Timeru
     *        Volat v app_main pred vytvorenim tasku
     * @param timeoutMs Timeout v milisekundach (default 5000ms)
     * @param panic Vyvolat panic pri timeout (default true)
     */
    static void initWatchdog(uint32_t timeoutMs = 5000, bool panic = true);

    /**
     * @brief Vrati cas od startu v milisekundach
     * @return Cas v ms
     */
    static uint64_t millis();

    /**
     * @brief Vrati cas od startu v mikrosekundach
     * @return Cas v us
     */
    static uint64_t micros();

    /**
     * @brief Staticka delay funkce (bez watchdog resetu)
     * @param ms Cas v milisekundach
     */
    static void delayMs(uint32_t ms);

    /**
     * @brief Staticka delay funkce (bez watchdog resetu)
     * @param us Cas v mikrosekundach
     */
    static void delayUs(uint32_t us);

    // --- Mutex pro sdilena data ---

    /**
     * @brief Inicializace globalniho mutexu
     *        Volat v app_main pred vytvorenim tasku
     */
    static void initMutex();

    /**
     * @brief Zamkne mutex (ceka dokud neni dostupny)
     * @return true pokud se podarilo zamknout
     */
    static bool lock();

    /**
     * @brief Zamkne mutex s timeoutem
     * @param timeoutMs Maximalni cas cekani v ms
     * @return true pokud se podarilo zamknout
     */
    static bool lock(uint32_t timeoutMs);

    /**
     * @brief Odemkne mutex
     */
    static void unlock();

private:
    static SemaphoreHandle_t _mutex;
    const char *_tag;
    uint32_t _delayMs;
    uint32_t _lastRunTime;
    uint64_t _startTime;
    bool _useWatchdog;
    bool _useCompensation;
};
