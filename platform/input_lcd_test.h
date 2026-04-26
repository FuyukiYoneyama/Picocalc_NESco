/*
 * input_lcd_test.h — PicoCalc LCD + keyboard smoke test
 *
 * Renders a simple full-screen text UI and echoes keyboard input so the
 * panel path and keyboard I2C path can be verified together on hardware.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void picocalc_input_lcd_test(void);

#ifdef __cplusplus
}
#endif
