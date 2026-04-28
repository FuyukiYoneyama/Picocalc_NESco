#include "core1_worker.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include <stdio.h>

enum {
    CORE1_STATUS_IDLE_ACK = 1u << 0,
};

enum {
    KEY_STATE_PRESSED = 1,
    KEY_STATE_HOLD = 2,
    KEY_STATE_RELEASED = 3,
};

enum {
    KEY_UP = 0xB5,
    KEY_DOWN = 0xB6,
    KEY_LEFT = 0xB4,
    KEY_RIGHT = 0xB7,
    KEY_BACKTICK = '`',
    KEY_MINUS = '-',
    KEY_LBRACK = '[',
    KEY_RBRACK = ']',
    KEY_W_UPPER = 'W',
    KEY_ESC = 0xB1,
    KEY_F1 = 0x81,
    KEY_F5 = 0x85,
};

enum {
    NES_A = (1u << 0),
    NES_B = (1u << 1),
    NES_SELECT = (1u << 2),
    NES_START = (1u << 3),
    NES_UP = (1u << 4),
    NES_DOWN = (1u << 5),
    NES_LEFT = (1u << 6),
    NES_RIGHT = (1u << 7),
};

enum {
    PAD_SYS_QUIT_LOCAL = 1,
    PAD_SYS_RESET_LOCAL = 0x80,
    PAD_SYS_VIEW_TOGGLE_LOCAL = 0x100,
    PAD_SYS_SCREENSHOT_LOCAL = 0x200,
};

enum {
    CORE1_KEYBOARD_MAX_EVENTS_PER_TICK = 4,
    CORE1_KEYBOARD_POLL_INTERVAL_MS = 4,
    CORE1_IDLE_POLL_INTERVAL_MS = 1,
};

static bool s_core1_worker_started = false;
static volatile unsigned s_core1_requested_services = 0;
static volatile unsigned s_core1_status = 0;
static critical_section_t s_core1_input_lock;
static DWORD s_core1_pad1_level = 0;
static DWORD s_core1_pad2_level = 0;
static DWORD s_core1_system_pending = 0;
static unsigned s_core1_input_event_count = 0;

extern BYTE i2c_kbd_read_key(void);
extern BYTE i2c_kbd_last_state(void);

static DWORD core1_map_key(BYTE key) {
    switch (key) {
    case KEY_UP:       return NES_UP;
    case KEY_DOWN:     return NES_DOWN;
    case KEY_LEFT:     return NES_LEFT;
    case KEY_RIGHT:    return NES_RIGHT;
    case KEY_BACKTICK: return NES_SELECT;
    case KEY_MINUS:    return NES_START;
    case KEY_LBRACK:   return NES_B;
    case KEY_RBRACK:   return NES_A;
    default:           return 0;
    }
}

static void core1_reset_keyboard_snapshot(void) {
    critical_section_enter_blocking(&s_core1_input_lock);
    s_core1_pad1_level = 0;
    s_core1_pad2_level = 0;
    s_core1_system_pending = 0;
    s_core1_input_event_count = 0;
    critical_section_exit(&s_core1_input_lock);
}

static void core1_keyboard_poll_once(void) {
    BYTE key;

    for (unsigned drained = 0; drained < CORE1_KEYBOARD_MAX_EVENTS_PER_TICK; drained++) {
        key = i2c_kbd_read_key();
        if (key == 0) {
            break;
        }

        const BYTE state = i2c_kbd_last_state();
        const DWORD mask = core1_map_key(key);
        DWORD system_bits = 0;

        if (state == KEY_STATE_PRESSED) {
            if (key == KEY_W_UPPER) {
                system_bits |= PAD_SYS_VIEW_TOGGLE_LOCAL;
            } else if (key == KEY_ESC) {
                system_bits |= PAD_SYS_QUIT_LOCAL;
            } else if (key == KEY_F5) {
                system_bits |= PAD_SYS_SCREENSHOT_LOCAL;
            } else if (key == KEY_F1) {
                system_bits |= PAD_SYS_RESET_LOCAL;
            }
        } else if (state == KEY_STATE_HOLD) {
            if (key == KEY_ESC) {
                system_bits |= PAD_SYS_QUIT_LOCAL;
            } else if (key == KEY_F1) {
                system_bits |= PAD_SYS_RESET_LOCAL;
            }
        }

        critical_section_enter_blocking(&s_core1_input_lock);
        if (mask != 0) {
            if (state == KEY_STATE_RELEASED) {
                s_core1_pad1_level &= ~mask;
            } else if (state == KEY_STATE_PRESSED || state == KEY_STATE_HOLD) {
                s_core1_pad1_level |= mask;
            }
        }
        s_core1_system_pending |= system_bits;
        s_core1_input_event_count++;
        critical_section_exit(&s_core1_input_lock);
    }
}

static void core1_worker_main(void) {
    multicore_lockout_victim_init();

    for (;;) {
        const unsigned services = s_core1_requested_services;
        if ((services & CORE1_SERVICE_KEYBOARD) != 0) {
            s_core1_status &= ~CORE1_STATUS_IDLE_ACK;
            core1_keyboard_poll_once();
        } else if (services == 0) {
            s_core1_status |= CORE1_STATUS_IDLE_ACK;
        } else {
            s_core1_status &= ~CORE1_STATUS_IDLE_ACK;
        }
        sleep_ms((services & CORE1_SERVICE_KEYBOARD) != 0 ?
                 CORE1_KEYBOARD_POLL_INTERVAL_MS :
                 CORE1_IDLE_POLL_INTERVAL_MS);
    }
}

void core1_worker_init(void) {
    if (s_core1_worker_started) {
        return;
    }
    critical_section_init(&s_core1_input_lock);
    s_core1_worker_started = true;
    multicore_launch_core1(core1_worker_main);
}

void core1_set_services(unsigned services) {
    const unsigned previous_services = s_core1_requested_services;
    if ((services & CORE1_SERVICE_KEYBOARD) != 0 &&
        (previous_services & CORE1_SERVICE_KEYBOARD) == 0) {
        core1_reset_keyboard_snapshot();
    }

    s_core1_requested_services = services;
    if (services != 0) {
        s_core1_status &= ~CORE1_STATUS_IDLE_ACK;
    }
}

bool core1_wait_idle_ack(unsigned timeout_ms) {
    const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    do {
        if ((s_core1_status & CORE1_STATUS_IDLE_ACK) != 0) {
            return true;
        }
        sleep_ms(1);
    } while (absolute_time_diff_us(get_absolute_time(), deadline) > 0);

    printf("[CORE1] idle ack timeout services=%u status=%u\n",
           s_core1_requested_services,
           s_core1_status);
    fflush(stdout);
    return false;
}

bool core1_keyboard_snapshot_active(void) {
    return (s_core1_requested_services & CORE1_SERVICE_KEYBOARD) != 0;
}

void core1_keyboard_read_snapshot(DWORD *pad1, DWORD *pad2, DWORD *system) {
    DWORD pad1_level;
    DWORD pad2_level;
    DWORD system_pending;

    critical_section_enter_blocking(&s_core1_input_lock);
    pad1_level = s_core1_pad1_level;
    pad2_level = s_core1_pad2_level;
    system_pending = s_core1_system_pending;
    s_core1_system_pending = 0;
    critical_section_exit(&s_core1_input_lock);

    *pad1 = pad1_level;
    *pad2 = pad2_level;
    *system = system_pending;
}

unsigned core1_keyboard_consume_event_count(void) {
    unsigned count;

    critical_section_enter_blocking(&s_core1_input_lock);
    count = s_core1_input_event_count;
    s_core1_input_event_count = 0;
    critical_section_exit(&s_core1_input_lock);

    return count;
}
