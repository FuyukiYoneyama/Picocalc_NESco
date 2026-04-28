/*
 * main.c — PicoCalc NES emulator entry point
 *
 * Sequence:
 *   1. RP2040 hardware init (250 MHz, UART, SPI, I2C)
 *   2. display_init()   — LCD + RGB444→RGB565 LUT
 *   3. input_init()     — I2C keyboard
 *   4. audio_init()     — PWM/DMA audio hardware (power-on init)
 *   5. opening screen   — splash + short wait / key skip
 *   6. rom_image_init() — ROM storage detection
 *   7. ROM menu loop    — choose ROM, run InfoNES session, return on ESC
 *
 * Part of Picocalc_NESco
 * MIT License
 */

#include "display.h"
#include "audio.h"
#include "core1_worker.h"
#include "infones_bridge.h"
#include "input.h"
#include "rom_menu.h"
#include "rom_image.h"
#include "runtime_log.h"
#include "version.h"

#include <malloc.h>
#include <stdio.h>

#ifdef PICO_BUILD
#  include "pico/stdlib.h"
#  include "hardware/clocks.h"
#endif

#ifdef PICO_BUILD
static void boot_log_stage(const char *stage) {
    NESCO_LOGF("[BOOT] %s\r\n", stage);
}

extern BYTE i2c_kbd_read_key(void);

static void boot_wait_opening_or_key(uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

    while (absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
        if (i2c_kbd_read_key() != 0) {
            break;
        }
        sleep_ms(10);
    }
}
#endif

int main(void) {
#ifdef PICO_BUILD
    extern size_t __malloc_trim_threshold;

    /* Set system clock to 250 MHz (per specs/90_integrated_spec.md) */
    set_sys_clock_khz(250000, true);
    stdio_init_all();
    printf("%s\r\n", PICOCALC_NESCO_BANNER_FULL);
    __malloc_trim_threshold = 0x200u;
#if defined(NESCO_RUNTIME_LOGS)
    extern size_t __malloc_top_pad;
    NESCO_LOGF("[BOOT] malloc trim_threshold=%lu top_pad=%lu\r\n",
               (unsigned long)__malloc_trim_threshold,
               (unsigned long)__malloc_top_pad);
#endif
    boot_log_stage("stdio ready");
#endif
    core1_worker_init();
#ifdef PICO_BUILD
    boot_log_stage("core1 worker ready");
#endif

    /* Hardware / peripheral init */
#ifdef PICO_BUILD
    boot_log_stage("display_init begin");
#endif
    display_init();
#ifdef PICO_BUILD
    boot_log_stage("display_init end");
    boot_log_stage("input_init begin");
#endif
    input_init();
#ifdef PICO_BUILD
    boot_log_stage("input_init end");
    boot_log_stage("audio_init begin");
#endif
    audio_init();
#ifdef PICO_BUILD
    boot_log_stage("audio_init end");
    boot_log_stage("opening begin");
#endif
    display_show_opening_screen();
#ifdef PICO_BUILD
    boot_wait_opening_or_key(3000);
    boot_log_stage("opening end");
    boot_log_stage("rom_image_init begin");
#endif
    rom_image_init();
#ifdef PICO_BUILD
    boot_log_stage("rom_image_init end");
    rom_image_log_heap_estimate("build");
    rom_image_log_heap("before rom_menu loop");
    boot_log_stage("rom_menu begin");
#endif

    for (;;) {
        NESCO_LOGF("[BOOT] enter rom_menu loop\r\n");
        const char *rom_path = picocalc_rom_menu();
        NESCO_LOGF("[BOOT] rom_menu returned path=%s\r\n", rom_path ? rom_path : "(null)");
        rom_image_set_selected_path(rom_path);
        NESCO_LOGF("[BOOT] run_infones_session begin\r\n");
        run_infones_session();
        NESCO_LOGF("[BOOT] run_infones_session end\r\n");
    }

    /* Should not reach here */
    for (;;) { }
    return 0;
}
