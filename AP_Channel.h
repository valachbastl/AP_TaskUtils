#pragma once

#include "AP_QueueBase.h"

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
class AP_Channel : private AP_QueueBase<T>
{
public:
    AP_Channel() : AP_QueueBase<T>(1) {}

    /**
     * @brief Write a new value (overwrites previous, never blocks)
     * @param value Value to write
     */
    void set(const T &value)
    {
        if (this->_queue) xQueueOverwrite(this->_queue, &value);
    }

    /**
     * @brief Write a new value from ISR context
     * @param value Value to write
     */
    void setFromISR(const T &value)
    {
        if (!this->_queue) return;
        BaseType_t woken = pdFALSE;
        xQueueOverwriteFromISR(this->_queue, &value, &woken);
        portYIELD_FROM_ISR(woken);
    }

    /**
     * @brief Read the current value (non-blocking, always returns latest)
     * @return Current value, or default T{} if never set
     */
    T get() const
    {
        T value{};
        if (this->_queue) xQueuePeek(this->_queue, &value, 0);
        return value;
    }

    /**
     * @brief Returns true if a value has been set at least once
     */
    bool isSet() const
    {
        if (!this->_queue) return false;
        T tmp{};
        return xQueuePeek(this->_queue, &tmp, 0) == pdTRUE;
    }
};
