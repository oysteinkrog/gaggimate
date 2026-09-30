// Host shim for freertos/queue.h.
//
// The simulator never spawns FreeRTOS tasks (see task.h), so a queue here has
// no consumer. xQueueCreate hands back a sentinel so the caller's "queue is
// present" checks pass, xQueueSend accepts and drops the item, and
// xQueueReceive never returns one. Code that needs its worker's effect on the
// simulator must do the work inline under GAGGIMATE_SIM instead.
#pragma once

#include "FreeRTOS.h"
#include <stddef.h>
#include <stdint.h>

typedef void *QueueHandle_t;
typedef QueueHandle_t xQueueHandle;

#define GM_SIM_QUEUE_HANDLE ((QueueHandle_t)0x1)

static inline QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize) {
    (void)length;
    (void)itemSize;
    return GM_SIM_QUEUE_HANDLE;
}

static inline void vQueueDelete(QueueHandle_t queue) { (void)queue; }

static inline BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    (void)queue;
    (void)item;
    (void)wait;
    return pdTRUE;
}

static inline BaseType_t xQueueSendToBack(QueueHandle_t queue, const void *item, TickType_t wait) {
    return xQueueSend(queue, item, wait);
}

static inline BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait) {
    (void)queue;
    (void)item;
    (void)wait;
    return pdFALSE;
}

static inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
    (void)queue;
    return 0;
}
