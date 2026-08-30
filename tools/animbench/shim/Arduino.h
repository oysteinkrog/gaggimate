// Host-build shim for tools/animbench: just the Arduino bits bganim touches.
#pragma once
#include <stdint.h>
#include <stdio.h>

#define log_i(fmt, ...) fprintf(stderr, "[I] " fmt "\n", ##__VA_ARGS__)
#define log_w(fmt, ...) fprintf(stderr, "[W] " fmt "\n", ##__VA_ARGS__)
#define log_e(fmt, ...) fprintf(stderr, "[E] " fmt "\n", ##__VA_ARGS__)
// Verbose tier is noise on the host: the placement decisions it narrates
// (SRAM vs PSRAM) are meaningless under plain malloc.
#define log_d(fmt, ...) ((void)0)
