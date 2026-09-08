// Host shim for freertos/FreeRTOS.h: minimal types/constants for the simulator.
#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
// Byte-granular on the Xtensa port, which is what stack sizes here are counted
// in. Only used for sizeof() in default stack-size expressions.
typedef uint8_t StackType_t;

#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY ((TickType_t)0xffffffff)
#define portTICK_PERIOD_MS ((TickType_t)1)
#define portTICK_RATE_MS portTICK_PERIOD_MS
#define configMINIMAL_STACK_SIZE 768
#define tskNO_AFFINITY 0x7fffffff
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTICKS_TO_MS(t) ((uint32_t)(t))

// No-op critical section: the simulator has no second core and no ISRs, and
// everything (LVGL, the web server, TouchInject) runs cooperatively on one
// main-loop thread (see task.h above), so there is never a concurrent writer
// to lock out. Real content, not just enough to link: TouchInject.cpp is
// written once against this pair and the device's real spinlock.
typedef struct {
    int _unused;
} portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED                                                                                             \
    {}
static inline void portENTER_CRITICAL(portMUX_TYPE *mux) { (void)mux; }
static inline void portEXIT_CRITICAL(portMUX_TYPE *mux) { (void)mux; }
