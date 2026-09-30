// Host shim for freertos/idf_additions.h.
//
// xTaskCreatePinnedToCoreWithCaps is the IDF extension that places a task's
// stack in a heap with the given caps (PSRAM for the service tasks, see
// CLAUDE.md). The simulator spawns no FreeRTOS tasks (task.h), so it is the
// same no-op as xTaskCreatePinnedToCore and hands back the sentinel handle.
#pragma once

#include "task.h"
#include <stdint.h>

static inline BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t fn, const char *name, uint32_t stack, void *param,
                                                         UBaseType_t prio, TaskHandle_t *handle, BaseType_t core,
                                                         uint32_t caps) {
    (void)caps;
    return xTaskCreatePinnedToCore(fn, name, stack, param, prio, handle, core);
}

static inline void vTaskDeleteWithCaps(TaskHandle_t handle) { (void)handle; }
