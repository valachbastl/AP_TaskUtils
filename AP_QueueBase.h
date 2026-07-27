#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* ========================================================================== */
/*  AP_QueueBase<T> — internal helper, not part of the public API.            */
/*  Shared queue handle lifecycle for AP_Channel<T> and AP_Queue<T>.          */
/* ========================================================================== */

template<typename T>
class AP_QueueBase
{
protected:
    explicit AP_QueueBase(size_t capacity)
    {
        _queue = xQueueCreate(capacity, sizeof(T));
    }

    ~AP_QueueBase()
    {
        if (_queue) vQueueDelete(_queue);
    }

    AP_QueueBase(const AP_QueueBase &) = delete;
    AP_QueueBase &operator=(const AP_QueueBase &) = delete;

    QueueHandle_t _queue = nullptr;
};
