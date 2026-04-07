#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* ========================================================================== */
/*  AP_Channel<T> — shared state (1 writer, N readers)                        */
/*                                                                             */
/*  Internally a single-item queue with xQueueOverwrite — FreeRTOS handles    */
/*  thread safety without an external mutex. Works from ISR (setFromISR).     */
/*                                                                             */
/*  Usage:                                                                     */
/*    AP_Channel<SensorData> sensorData;   // globally in interface.h         */
/*    sensorData.set({.temp = 23.5f});     // 1 writer                        */
/*    SensorData d = sensorData.get();     // N readers                       */
/* ========================================================================== */

template<typename T>
class AP_Channel
{
public:
    AP_Channel()
    {
        _queue = xQueueCreate(1, sizeof(T));
    }

    ~AP_Channel()
    {
        if (_queue) vQueueDelete(_queue);
    }

    AP_Channel(const AP_Channel &) = delete;
    AP_Channel &operator=(const AP_Channel &) = delete;

    /**
     * @brief Write a new value (overwrites previous, never blocks)
     * @param value Value to write
     */
    void set(const T &value)
    {
        xQueueOverwrite(_queue, &value);
    }

    /**
     * @brief Write a new value from ISR context
     * @param value Value to write
     */
    void setFromISR(const T &value)
    {
        BaseType_t woken = pdFALSE;
        xQueueOverwriteFromISR(_queue, &value, &woken);
        portYIELD_FROM_ISR(woken);
    }

    /**
     * @brief Read the current value (non-blocking, always returns latest)
     * @return Current value, or default T{} if never set
     */
    T get() const
    {
        T value{};
        xQueuePeek(_queue, &value, 0);
        return value;
    }

    /**
     * @brief Returns true if a value has been set at least once
     */
    bool isSet() const
    {
        T tmp{};
        return xQueuePeek(_queue, &tmp, 0) == pdTRUE;
    }

private:
    QueueHandle_t _queue = nullptr;
};
