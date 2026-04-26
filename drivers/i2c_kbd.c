/*
 * i2c_kbd.c — PicoCalc keyboard I2C driver
 *
 * The PicoCalc keyboard controller is an STM32F103 that exposes a simple
 * register-oriented I2C slave protocol. The Pico side reads the FIFO depth
 * register first, then dequeues one {state, key} item at a time.
 */

#include "common_types.h"

#ifdef PICO_BUILD
#  include "hardware/gpio.h"
#  include "hardware/i2c.h"
#endif

enum {
    KBD_I2C_ADDR = 0x1Fu,
    KBD_REG_KEY  = 0x04u,
    KBD_REG_BAT  = 0x0Bu,
    KBD_REG_FIFO = 0x09u,
};

enum {
    KBD_KEY_STATE_IDLE     = 0,
    KBD_KEY_STATE_PRESSED  = 1,
    KBD_KEY_STATE_HOLD     = 2,
    KBD_KEY_STATE_RELEASED = 3,
};

static BYTE s_last_state = KBD_KEY_STATE_IDLE;

#ifdef PICO_BUILD
static int i2c_kbd_read_reg(BYTE reg, BYTE *buf, size_t len) {
    int rc = i2c_write_blocking(i2c1, KBD_I2C_ADDR, &reg, 1, true);
    if (rc != 1) {
        return -1;
    }

    rc = i2c_read_blocking(i2c1, KBD_I2C_ADDR, buf, len, false);
    return (rc == (int)len) ? 0 : -1;
}
#endif

int i2c_kbd_init(void) {
    s_last_state = KBD_KEY_STATE_IDLE;

#ifdef PICO_BUILD
    i2c_init(i2c1, 400 * 1000u);
    gpio_set_function(6, GPIO_FUNC_I2C);
    gpio_set_function(7, GPIO_FUNC_I2C);
    gpio_pull_up(6);
    gpio_pull_up(7);
#endif

    return 0;
}

BYTE i2c_kbd_read_key(void) {
    s_last_state = KBD_KEY_STATE_IDLE;

#ifdef PICO_BUILD
    BYTE key_info[2];
    BYTE fifo_item[2];

    if (i2c_kbd_read_reg(KBD_REG_KEY, key_info, sizeof(key_info)) != 0) {
        return 0;
    }

    if ((key_info[0] & 0x1Fu) == 0) {
        return 0;
    }

    if (i2c_kbd_read_reg(KBD_REG_FIFO, fifo_item, sizeof(fifo_item)) != 0) {
        return 0;
    }

    s_last_state = fifo_item[0];
    return fifo_item[1];
#else
    return 0;
#endif
}

BYTE i2c_kbd_last_state(void) {
    return s_last_state;
}

int i2c_kbd_read_battery(BYTE *value) {
    BYTE raw[2];

    if (value == NULL) {
        return -1;
    }

#ifdef PICO_BUILD
    if (i2c_kbd_read_reg(KBD_REG_BAT, raw, sizeof(raw)) != 0) {
        return -1;
    }

    *value = raw[1];
    return 0;
#else
    *value = 0;
    return -1;
#endif
}
