#ifdef GAGGIMATE_QEMU

#include <esp_bit_defs.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_private/startup_internal.h>
#include <hal/adc_types.h>
#include <stdbool.h>
#include <stdint.h>

// ADC self-calibration deadlocks under QEMU.
//
// esp_adc registers adc_hw_calibration() as a C constructor (adc_common.c,
// guarded only by SOC_ADC_CALIBRATION_V1_SUPPORTED, with no Kconfig escape), so
// it runs from do_global_ctors() before app_main. It ends up in
// read_cal_channel(), which spins on
//
//     while (!adc_oneshot_ll_get_event(event));
//
// that is, on SENS.sar_meas1_ctrl2.meas1_done_sar -- bit 16 of
// SENS_SAR_MEAS1_CTRL2_REG at 0x6000880C. QEMU has no ADC model; the SENS block
// is a read-only-zero unimplemented device, so the bit never sets and the guest
// hangs there forever, before a single line of application code runs.
//
// Interposing here is the only clean lever. adc_hal_self_calibration is a
// non-static global declared in hal/adc_hal_common.h, so -Wl,--wrap resolves it
// across the prebuilt archives; a weak override would not work, because
// adc_hal_common.o also defines adc_hal_calibration_init and pulling the whole
// object in would duplicate that symbol. The linker flag is added by the
// GAGGIMATE_QEMU_BUILD branch in src/CMakeLists.txt.
//
// Returning 0 means "no calibration offset", which is exactly what an
// uncalibrated part reports. Nothing in this firmware reads the ADC under QEMU
// anyway -- the pressure sensor lives on the controller board.
extern "C" uint32_t __wrap_adc_hal_self_calibration(adc_unit_t adc_n, adc_atten_t atten, bool internal_gnd) {
    (void)adc_n;
    (void)atten;
    (void)internal_gnd;
    return 0;
}

// Floating point in a global constructor panics under QEMU.
//
// The Xtensa FPU is coprocessor 0, gated by CPENABLE. FreeRTOS enables it
// lazily: the first FP instruction a task executes traps into _xt_coproc_exc,
// which asks XT_RTOS_CP_STATE for that task's coprocessor save area. Before the
// scheduler runs there is no task, the query returns 0, and the handler falls
// through to
//
//     .L_xt_coproc_invalid:
//         movi a0, PANIC_RSN_COPROCEXCEPTION
//
// which is the "Coprocessor exception" panic reporting EXCCAUSE 4 -- a pseudo
// cause the handler writes itself, not the architectural one. Global
// constructors run from do_global_ctors() inside start_cpu0_default(), long
// before vTaskStartScheduler, so any FP instruction there is fatal unless
// CPENABLE is already non-zero.
//
// Real hardware boots this firmware, so CPENABLE evidently comes out of the ROM
// non-zero there. QEMU resets it to 0, and the first static Profile to be
// copy-constructed (FLUSH_PROFILE, via static_profiles.h) dies on the "lsi f0"
// that loads a phase's float fields. Nothing in ESP-IDF writes CPENABLE during
// startup, so whatever the emulator resets it to is what the guest gets.
//
// Enabling coprocessor 0 for the startup context fixes it and leaks nothing:
// the first task switch reloads CPENABLE from the incoming task's save area, so
// per-task lazy FPU management still behaves exactly as it does on the device.
// Priority 200 puts this after every ESP-IDF core init function (the last is
// init_xt_wdt at 170) and before do_global_ctors().
ESP_SYSTEM_INIT_FN(qemu_enable_fpu, CORE, BIT(0), 200) {
    uint32_t previous = 0;
    __asm__ __volatile__("rsr.cpenable %0" : "=r"(previous));
    __asm__ __volatile__("wsr.cpenable %0; rsync" ::"r"(1u));
    // The old value is the entire justification for this shim, so log it: if a
    // later QEMU models the ROM's CPENABLE, this prints 1 and the shim is dead
    // weight.
    ESP_EARLY_LOGI("QemuShims", "CPENABLE was 0x%02x, forcing FPU on for startup", (unsigned)previous);
    return ESP_OK;
}

#endif // GAGGIMATE_QEMU
