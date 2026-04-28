#include "core1_worker.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include <stdio.h>

enum {
    CORE1_STATUS_IDLE_ACK = 1u << 0,
};

static bool s_core1_worker_started = false;
static volatile unsigned s_core1_requested_services = 0;
static volatile unsigned s_core1_status = 0;

static void core1_worker_main(void) {
    for (;;) {
        const unsigned services = s_core1_requested_services;
        if (services == 0) {
            s_core1_status |= CORE1_STATUS_IDLE_ACK;
        } else {
            s_core1_status &= ~CORE1_STATUS_IDLE_ACK;
        }
        sleep_ms(1);
    }
}

void core1_worker_init(void) {
    if (s_core1_worker_started) {
        return;
    }
    s_core1_worker_started = true;
    multicore_launch_core1(core1_worker_main);
}

void core1_set_services(unsigned services) {
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
