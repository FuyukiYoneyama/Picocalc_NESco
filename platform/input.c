/*
 * input.c — I2C keyboard input mapping (PicoCalc)
 *
 * Key mapping (PicoCalc keyboard → NES buttons):
 *   Arrow keys         → D-Pad (Up/Down/Left/Right)
 *   `                  → Select
 *   -                  → Start
 *   [                  → B button
 *   ]                  → A button
 *   Escape             → PAD_SYS_QUIT
 *   F1 / Ctrl+R        → PAD_SYS_RESET
 *   F5                → PAD_SYS_SCREENSHOT
 *   Shift+W            → PAD_SYS_VIEW_TOGGLE
 *
 * Part of Picocalc_NESco
 * MIT License
 */

#include "input.h"
#include "InfoNES.h"

#if defined(NESCO_RUNTIME_LOGS)
#include <stdio.h>
#endif

/* =====================================================================
 *  Platform I2C keyboard scan (implement in drivers/i2c_kbd.c)
 * ===================================================================== */
extern "C" int  i2c_kbd_init(void);
extern "C" BYTE i2c_kbd_read_key(void);   /* Returns 0 if no key; scancode otherwise */
extern "C" BYTE i2c_kbd_last_state(void);

enum {
    KEY_STATE_IDLE     = 0,
    KEY_STATE_PRESSED  = 1,
    KEY_STATE_HOLD     = 2,
    KEY_STATE_RELEASED = 3,
};

/* PicoCalc keyboard controller key codes */
#define KEY_UP      0xB5
#define KEY_DOWN    0xB6
#define KEY_LEFT    0xB4
#define KEY_RIGHT   0xB7
#define KEY_BACKTICK '`'
#define KEY_MINUS   '-'
#define KEY_LBRACK  '['
#define KEY_RBRACK  ']'
#define KEY_W_UPPER 'W'
#define KEY_ESC     0xB1
#define KEY_F1      0x81
#define KEY_F5      0x85

/* NES pad bit positions */
#define NES_A       (1u << 0)
#define NES_B       (1u << 1)
#define NES_SELECT  (1u << 2)
#define NES_START   (1u << 3)
#define NES_UP      (1u << 4)
#define NES_DOWN    (1u << 5)
#define NES_LEFT    (1u << 6)
#define NES_RIGHT   (1u << 7)

static DWORD s_pad1_state = 0;

#if defined(NESCO_RUNTIME_LOGS)
static unsigned long s_input_poll_seq = 0;
static unsigned s_input_nonzero_polls = 0;
static DWORD s_input_last_pad1 = 0;

static void input_log_summary(unsigned events,
                              BYTE first_key,
                              BYTE first_state,
                              BYTE last_key,
                              BYTE last_state,
                              DWORD before,
                              DWORD after,
                              DWORD sys_bits)
{
    bool emit = false;

    if (events != 0 || before != after || sys_bits != 0) {
        emit = true;
    }

    if (after != 0) {
        if (s_input_nonzero_polls < 0xffffu) {
            s_input_nonzero_polls++;
        }
        if (s_input_nonzero_polls >= 60u) {
            emit = true;
        }
    } else {
        s_input_nonzero_polls = 0;
    }

    if (!emit) {
        s_input_last_pad1 = after;
        return;
    }

    printf("[KEY_SUM] seq=%lu events=%u first=%02X/%u last=%02X/%u before=%08lX after=%08lX last=%08lX sys=%08lX hold=%u\n",
           s_input_poll_seq,
           events,
           (unsigned)first_key,
           (unsigned)first_state,
           (unsigned)last_key,
           (unsigned)last_state,
           (unsigned long)before,
           (unsigned long)after,
           (unsigned long)s_input_last_pad1,
           (unsigned long)sys_bits,
           s_input_nonzero_polls);

    s_input_last_pad1 = after;
    if (after != 0) {
        s_input_nonzero_polls = 0;
    }
}
#endif

static DWORD input_map_key(BYTE key) {
    switch (key) {
    case KEY_UP:      return NES_UP;
    case KEY_DOWN:    return NES_DOWN;
    case KEY_LEFT:    return NES_LEFT;
    case KEY_RIGHT:   return NES_RIGHT;
    case KEY_BACKTICK:return NES_SELECT;
    case KEY_MINUS:   return NES_START;
    case KEY_LBRACK:  return NES_B;
    case KEY_RBRACK:  return NES_A;
    default:          return 0;
    }
}

void input_init(void) {
    i2c_kbd_init();
    s_pad1_state = 0;
#if defined(NESCO_RUNTIME_LOGS)
    s_input_poll_seq = 0;
    s_input_nonzero_polls = 0;
    s_input_last_pad1 = 0;
#endif
}

void input_poll(DWORD *pad1, DWORD *pad2, DWORD *system) {
    DWORD sys_bits = 0;
#if defined(NESCO_RUNTIME_LOGS)
    const DWORD before = s_pad1_state;
    unsigned events = 0;
    BYTE first_key = 0;
    BYTE first_state = 0;
    BYTE last_key = 0;
    BYTE last_state = 0;
    s_input_poll_seq++;
#endif

    BYTE key;
    while ((key = i2c_kbd_read_key()) != 0) {
        BYTE state = i2c_kbd_last_state();
        DWORD mask = input_map_key(key);
#if defined(NESCO_RUNTIME_LOGS)
        if (events == 0) {
            first_key = key;
            first_state = state;
        }
        events++;
        last_key = key;
        last_state = state;
#endif

        if (mask != 0) {
            if (state == KEY_STATE_RELEASED) {
                s_pad1_state &= ~mask;
            } else if (state == KEY_STATE_PRESSED || state == KEY_STATE_HOLD) {
                s_pad1_state |= mask;
            }
        }

        if (state == KEY_STATE_PRESSED) {
            if (key == KEY_W_UPPER) {
                sys_bits |= PAD_SYS_VIEW_TOGGLE;
            } else if (key == KEY_ESC) {
                sys_bits |= PAD_SYS_QUIT;
            } else if (key == KEY_F5) {
                sys_bits |= PAD_SYS_SCREENSHOT;
            } else if (key == KEY_F1) {
                sys_bits |= PAD_SYS_RESET;
            }
        } else if (state == KEY_STATE_HOLD) {
            if (key == KEY_ESC) {
                sys_bits |= PAD_SYS_QUIT;
            } else if (key == KEY_F1) {
                sys_bits |= PAD_SYS_RESET;
            }
        }
    }

    *pad1 = s_pad1_state;
    *pad2 = 0;
    *system = sys_bits;
#if defined(NESCO_RUNTIME_LOGS)
    input_log_summary(events,
                      first_key,
                      first_state,
                      last_key,
                      last_state,
                      before,
                      s_pad1_state,
                      sys_bits);
#endif
}

/* =====================================================================
 *  InfoNES_PadState — called by PPU_HSync at VBlank
 * ===================================================================== */
void InfoNES_PadState(DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem) {
    input_poll(pdwPad1, pdwPad2, pdwSystem);
}

int getbuttons(void) {
    return 0;
}
