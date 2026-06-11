#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

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
class AP_Queue
{
public:
    /**
     * @brief Constructor
     * @param capacity Maximum number of items in the queue
     */
    explicit AP_Queue(size_t capacity)
    {
        _queue = xQueueCreate(capacity, sizeof(T));
    }

    ~AP_Queue()
    {
        if (_queue) vQueueDelete(_queue);
    }

    AP_Queue(const AP_Queue &) = delete;
    AP_Queue &operator=(const AP_Queue &) = delete;

    /**
     * @brief Send an item (blocking — waits until space is available)
     * @return true on success
     */
    bool send(const T &item)
    {
        return _queue && xQueueSend(_queue, &item, portMAX_DELAY) == pdTRUE;
    }

    /**
     * @brief Send an item with timeout
     * @param item      Item to send
     * @param timeoutMs Maximum wait time in ms (0 = non-blocking)
     * @return true on success
     */
    bool send(const T &item, uint32_t timeoutMs)
    {
        return _queue && xQueueSend(_queue, &item, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    }

    /**
     * @brief Send an item from ISR context
     * @return true on success
     */
    bool sendFromISR(const T &item)
    {
        if (!_queue) return false;
        BaseType_t woken = pdFALSE;
        bool ok = xQueueSendFromISR(_queue, &item, &woken) == pdTRUE;
        portYIELD_FROM_ISR(woken);
        return ok;
    }

    /**
     * @brief Receive an item (blocking — waits until an item is available)
     * @return true on success
     */
    bool receive(T &item)
    {
        return _queue && xQueueReceive(_queue, &item, portMAX_DELAY) == pdTRUE;
    }

    /**
     * @brief Receive an item with timeout
     * @param item      Output item
     * @param timeoutMs Maximum wait time in ms (0 = non-blocking)
     * @return true on success
     */
    bool receive(T &item, uint32_t timeoutMs)
    {
        return _queue && xQueueReceive(_queue, &item, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    }

    /** @brief Returns number of items waiting in the queue */
    size_t available() const
    {
        return _queue ? (size_t)uxQueueMessagesWaiting(_queue) : 0;
    }

    /** @brief Flush the queue */
    void clear()
    {
        if (_queue) xQueueReset(_queue);
    }

private:
    QueueHandle_t _queue = nullptr;
};
