#pragma once

#include "AP_QueueBase.h"
#include <type_traits>

/* ========================================================================== */
/*  AP_Queue<T> — event stream (N writers, 1 reader)                          */
/*                                                                             */
/*  Thin wrapper over FreeRTOS xQueue — thread safety handled by FreeRTOS.   */
/*                                                                             */
/*  Usage:                                                                     */
/*    AP_Queue<ErrorEvent> errorQueue(10); // globally in interface.h         */
/*    errorQueue.send({.source = MQTT});   // N writers                       */
/*    ErrorEvent e;                                                            */
/*    if (errorQueue.receive(e)) { ... }  // 1 reader                        */
/* ========================================================================== */

template<typename T>
class AP_Queue : private AP_QueueBase<T>
{
public:
    // FreeRTOS queues memcpy the raw bytes of T — a non-trivially-copyable T (owning
    // pointers, std::string/std::vector, a user-defined copy ctor/dtor) would compile
    // silently and corrupt/double-free at runtime instead of failing here.
    static_assert(std::is_trivially_copyable<T>::value,
                  "AP_Queue<T>: T must be trivially copyable (plain data struct) — "
                  "FreeRTOS queues copy it byte-for-byte, bypassing C++ copy semantics");

    /**
     * @brief Constructor
     * @param capacity Maximum number of items in the queue
     */
    explicit AP_Queue(size_t capacity) : AP_QueueBase<T>(capacity) {}

    /**
     * @brief Send an item (blocking — waits until space is available)
     * @return true on success
     */
    bool send(const T &item)
    {
        return this->_queue && xQueueSend(this->_queue, &item, portMAX_DELAY) == pdTRUE;
    }

    /**
     * @brief Send an item with timeout
     * @param item      Item to send
     * @param timeoutMs Maximum wait time in ms (0 = non-blocking)
     * @return true on success
     */
    bool send(const T &item, uint32_t timeoutMs)
    {
        return this->_queue && xQueueSend(this->_queue, &item, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    }

    /**
     * @brief Send an item from ISR context
     * @return true on success
     */
    bool sendFromISR(const T &item)
    {
        if (!this->_queue) return false;
        BaseType_t woken = pdFALSE;
        bool ok = xQueueSendFromISR(this->_queue, &item, &woken) == pdTRUE;
        portYIELD_FROM_ISR(woken);
        return ok;
    }

    /**
     * @brief Receive an item (blocking — waits until an item is available)
     * @return true on success
     */
    bool receive(T &item)
    {
        return this->_queue && xQueueReceive(this->_queue, &item, portMAX_DELAY) == pdTRUE;
    }

    /**
     * @brief Receive an item with timeout
     * @param item      Output item
     * @param timeoutMs Maximum wait time in ms (0 = non-blocking)
     * @return true on success
     */
    bool receive(T &item, uint32_t timeoutMs)
    {
        return this->_queue && xQueueReceive(this->_queue, &item, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    }

    /** @brief Returns number of items waiting in the queue */
    size_t available() const
    {
        return this->_queue ? (size_t)uxQueueMessagesWaiting(this->_queue) : 0;
    }

    /** @brief Flush the queue */
    void clear()
    {
        if (this->_queue) xQueueReset(this->_queue);
    }
};
