#include "boko_flash_trace.h"

#include "ff.h"
#include "rom_image.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum {
    BOKO_TRACE_PAGE_BYTES = FLASH_SECTOR_SIZE,
    BOKO_TRACE_FLASH_OFFSET = 0x00102000u,
    BOKO_TRACE_CAPACITY_BYTES = 512u * 1024u,
    BOKO_TRACE_EVENT_BYTES = 64u,
    BOKO_TRACE_EVENTS_PER_PAGE = BOKO_TRACE_PAGE_BYTES / BOKO_TRACE_EVENT_BYTES,
    BOKO_TRACE_MAGIC = 0x464B4F42u, /* BOKF */
    BOKO_TRACE_VERSION = 2u,
};

#define BOKO_TAG(a, b, c, d) \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

enum {
    BOKO_TAG_HEADER = BOKO_TAG('H', 'D', 'R', '0'),
    BOKO_TAG_2C = BOKO_TAG('2', 'C', '0', '0'),
    BOKO_TAG_STATE = BOKO_TAG('S', 'T', 'A', 'T'),
    BOKO_TAG_8827 = BOKO_TAG('8', '8', '2', '7'),
    BOKO_TAG_888E = BOKO_TAG('8', '8', '8', 'E'),
    BOKO_TAG_HEARTBEAT = BOKO_TAG('H', 'B', '0', '0'),
    BOKO_TAG_FREEZE = BOKO_TAG('F', 'R', 'Z', 'E'),
    BOKO_TAG_END = BOKO_TAG('E', 'N', 'D', '0'),
};

typedef struct {
    uint32_t tag;
    uint32_t seq;
    uint32_t t_us;
    uint32_t frame;
    uint32_t p0;
    uint32_t p1;
    uint32_t p2;
    uint32_t p3;
    uint16_t pc;
    uint16_t ret;
    uint16_t scanline;
    uint16_t reserved16;
    BYTE a;
    BYTE flags;
    BYTE x;
    BYTE y;
    BYTE sp;
    BYTE r1b;
    BYTE r1f;
    BYTE r25;
    BYTE r26;
    BYTE r29;
    BYTE r2c;
    BYTE r3b;
    BYTE r3c;
    BYTE r3f;
    BYTE r48;
    BYTE r9d;
    BYTE value;
    BYTE prev;
    BYTE extra0;
    BYTE extra1;
    BYTE reserved[4];
} boko_trace_event_t;

static_assert(sizeof(boko_trace_event_t) == BOKO_TRACE_EVENT_BYTES,
              "Bokosuka flash trace event must stay 64 bytes");

static BYTE s_page[BOKO_TRACE_PAGE_BYTES];
static uint32_t s_trace_offset;
static uint32_t s_write_offset;
static uint32_t s_page_used;
static uint32_t s_event_count;
static uint32_t s_committed_bytes;
static bool s_ready;
static bool s_full;
static bool s_dumped;

static uint32_t boko_trace_flash_offset(void)
{
    return BOKO_TRACE_FLASH_OFFSET;
}

static const BYTE *boko_trace_flash_ptr(void)
{
    return (const BYTE *)(XIP_BASE + boko_trace_flash_offset());
}

static char boko_trace_ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static bool boko_trace_contains_casefold(const char *text, const char *needle)
{
    const char *p;
    size_t needle_len;

    if (!text || !needle || needle[0] == '\0') {
        return false;
    }

    needle_len = strlen(needle);
    for (p = text; *p; ++p) {
        size_t i;
        for (i = 0; i < needle_len; ++i) {
            if (p[i] == '\0' ||
                boko_trace_ascii_lower(p[i]) != boko_trace_ascii_lower(needle[i])) {
                break;
            }
        }
        if (i == needle_len) {
            return true;
        }
    }
    return false;
}

static bool boko_trace_rom_is_target(const char *rom_path)
{
    return rom_path &&
           (boko_trace_contains_casefold(rom_path, "bokosuka") ||
            boko_trace_contains_casefold(rom_path, "boko"));
}

static void boko_trace_clear_page(void)
{
    memset(s_page, 0xFF, sizeof(s_page));
    s_page_used = 0;
}

static void boko_trace_put_event(const boko_trace_event_t *event)
{
    if (!s_ready || s_full || !event) {
        return;
    }

    if (s_page_used + sizeof(*event) > sizeof(s_page)) {
        boko_flash_trace_flush();
        if (!s_ready || s_full) {
            return;
        }
    }

    memcpy(&s_page[s_page_used], event, sizeof(*event));
    s_page_used += (uint32_t)sizeof(*event);
    s_event_count++;
}

static void boko_trace_init_event(boko_trace_event_t *event, uint32_t tag)
{
    memset(event, 0, sizeof(*event));
    event->tag = tag;
    event->t_us = time_us_32();
}

static bool boko_trace_build_path(char *path, unsigned path_size)
{
    FILINFO fno;
    FRESULT fr;
    unsigned index;

    if (!path || path_size == 0u || !rom_image_ensure_sd_mount()) {
        return false;
    }

    fr = f_mkdir("0:/traces");
    if (fr != FR_OK && fr != FR_EXIST) {
        return false;
    }

    for (index = 1u; index <= 9999u; ++index) {
        snprintf(path, path_size, "0:/traces/BOKO_%04u.BIN", index);
        fr = f_stat(path, &fno);
        if (fr == FR_NO_FILE || fr == FR_NO_PATH) {
            return true;
        }
    }

    snprintf(path, path_size, "0:/traces/BOKO_LAST.BIN");
    return true;
}

void boko_flash_trace_begin(const char *rom_path)
{
    boko_trace_event_t header;
    const uint32_t erase_offset = boko_trace_flash_offset();
    const uint32_t erase_bytes = BOKO_TRACE_CAPACITY_BYTES;
    const uint32_t begin_us = time_us_32();
    uint32_t irq;

    s_trace_offset = erase_offset;
    s_write_offset = 0;
    s_event_count = 0;
    s_committed_bytes = 0;
    s_ready = false;
    s_full = false;
    s_dumped = false;
    boko_trace_clear_page();

    if (!boko_trace_rom_is_target(rom_path)) {
        printf("[TRACE_SKIP] backend=xip_flash reason=not-bokosuka path=%s\r\n",
               rom_path ? rom_path : "(null)");
        fflush(stdout);
        return;
    }

    printf("[TRACE_ERASE_BEGIN] backend=xip_flash offset=0x%08lx bytes=%lu page=%u\r\n",
           (unsigned long)s_trace_offset,
           (unsigned long)erase_bytes,
           (unsigned)BOKO_TRACE_PAGE_BYTES);
    fflush(stdout);

    irq = save_and_disable_interrupts();
    flash_range_erase(erase_offset, erase_bytes);
    restore_interrupts(irq);

    printf("[TRACE_ERASE_END] ok=1 elapsed_us=%lu\r\n",
           (unsigned long)(time_us_32() - begin_us));
    fflush(stdout);

    s_ready = true;

    boko_trace_init_event(&header, BOKO_TAG_HEADER);
    header.seq = BOKO_TRACE_VERSION;
    header.p0 = BOKO_TRACE_MAGIC;
    header.p1 = BOKO_TRACE_CAPACITY_BYTES;
    header.p2 = BOKO_TRACE_PAGE_BYTES;
    header.p3 = BOKO_TRACE_EVENT_BYTES;
    boko_trace_put_event(&header);

    printf("[TRACE_READY] backend=xip_flash offset=0x%08lx capacity=%lu page=%u event=%u\r\n",
           (unsigned long)s_trace_offset,
           (unsigned long)BOKO_TRACE_CAPACITY_BYTES,
           (unsigned)BOKO_TRACE_PAGE_BYTES,
           (unsigned)BOKO_TRACE_EVENT_BYTES);
    fflush(stdout);
}

void boko_flash_trace_flush(void)
{
    uint32_t irq;
    const uint32_t begin_us = time_us_32();
    const uint32_t page_index = s_write_offset / BOKO_TRACE_PAGE_BYTES;

    if (!s_ready || s_page_used == 0u) {
        return;
    }

    if (s_write_offset + BOKO_TRACE_PAGE_BYTES > BOKO_TRACE_CAPACITY_BYTES) {
        s_full = true;
        printf("[TRACE_FULL] committed=%lu events=%lu\r\n",
               (unsigned long)s_committed_bytes,
               (unsigned long)s_event_count);
        fflush(stdout);
        return;
    }

    printf("[TRACE_FLASH_WRITE_BEGIN] page=%lu offset=0x%08lx used=%lu events=%lu t_us=%lu\r\n",
           (unsigned long)page_index,
           (unsigned long)(s_trace_offset + s_write_offset),
           (unsigned long)s_page_used,
           (unsigned long)s_event_count,
           (unsigned long)begin_us);
    fflush(stdout);

    irq = save_and_disable_interrupts();
    flash_range_program(s_trace_offset + s_write_offset, s_page, BOKO_TRACE_PAGE_BYTES);
    restore_interrupts(irq);

    s_write_offset += BOKO_TRACE_PAGE_BYTES;
    s_committed_bytes = s_write_offset;
    boko_trace_clear_page();

    printf("[TRACE_FLASH_WRITE_END] page=%lu committed=%lu elapsed_us=%lu\r\n",
           (unsigned long)page_index,
           (unsigned long)s_committed_bytes,
           (unsigned long)(time_us_32() - begin_us));
    fflush(stdout);
}

void boko_flash_trace_record_2c(unsigned long seq,
                                unsigned long frame,
                                unsigned scanline,
                                WORD pc,
                                WORD ret,
                                BYTE prev,
                                BYTE value,
                                DWORD pad1,
                                BYTE r3b,
                                BYTE r3c,
                                BYTE r9d)
{
    boko_trace_event_t event;

    boko_trace_init_event(&event, BOKO_TAG_2C);
    event.seq = (uint32_t)seq;
    event.frame = (uint32_t)frame;
    event.p0 = pad1;
    event.pc = pc;
    event.ret = ret;
    event.scanline = (uint16_t)scanline;
    event.r3b = r3b;
    event.r3c = r3c;
    event.r9d = r9d;
    event.value = value;
    event.prev = prev;
    boko_trace_put_event(&event);
}

void boko_flash_trace_record_state(unsigned long frame,
                                   unsigned scanline,
                                   WORD pc,
                                   WORD addr,
                                   BYTE value,
                                   DWORD pad1,
                                   DWORD pad2,
                                   BYTE r2c,
                                   BYTE r3b,
                                   BYTE r3c,
                                   BYTE r9d)
{
    boko_trace_event_t event;

    boko_trace_init_event(&event, BOKO_TAG_STATE);
    event.frame = (uint32_t)frame;
    event.p0 = pad1;
    event.p1 = pad2;
    event.pc = pc;
    event.ret = addr;
    event.scanline = (uint16_t)scanline;
    event.r2c = r2c;
    event.r3b = r3b;
    event.r3c = r3c;
    event.r9d = r9d;
    event.value = value;
    boko_trace_put_event(&event);
}

void boko_flash_trace_record_8827(unsigned long trig,
                                  unsigned window,
                                  unsigned long frame,
                                  unsigned scanline,
                                  BYTE a,
                                  BYTE flags,
                                  BYTE r1b,
                                  BYTE r25,
                                  BYTE r26,
                                  BYTE r2c,
                                  BYTE r3f,
                                  BYTE r48,
                                  BYTE r3b,
                                  BYTE r3c,
                                  BYTE r9d)
{
    boko_trace_event_t event;

    boko_trace_init_event(&event, BOKO_TAG_8827);
    event.seq = (uint32_t)trig;
    event.frame = (uint32_t)frame;
    event.p0 = window;
    event.scanline = (uint16_t)scanline;
    event.a = a;
    event.flags = flags;
    event.r1b = r1b;
    event.r25 = r25;
    event.r26 = r26;
    event.r2c = r2c;
    event.r3f = r3f;
    event.r48 = r48;
    event.r3b = r3b;
    event.r3c = r3c;
    event.r9d = r9d;
    boko_trace_put_event(&event);
}

void boko_flash_trace_record_888e(unsigned long trig,
                                  unsigned window,
                                  unsigned long frame,
                                  unsigned scanline,
                                  BYTE a,
                                  BYTE flags,
                                  BYTE r1f,
                                  BYTE r25,
                                  BYTE r26,
                                  BYTE r29,
                                  BYTE r2c,
                                  BYTE r3b,
                                  BYTE r3c)
{
    boko_trace_event_t event;

    boko_trace_init_event(&event, BOKO_TAG_888E);
    event.seq = (uint32_t)trig;
    event.frame = (uint32_t)frame;
    event.p0 = window;
    event.scanline = (uint16_t)scanline;
    event.a = a;
    event.flags = flags;
    event.r1f = r1f;
    event.r25 = r25;
    event.r26 = r26;
    event.r29 = r29;
    event.r2c = r2c;
    event.r3b = r3b;
    event.r3c = r3c;
    boko_trace_put_event(&event);
}

void boko_flash_trace_record_heartbeat(unsigned long seq,
                                       WORD pc,
                                       unsigned scanline,
                                       DWORD pad1,
                                       DWORD pad2,
                                       DWORD sys,
                                       BYTE r2c,
                                       BYTE r3b,
                                       BYTE r3c,
                                       BYTE r9d,
                                       BYTE a)
{
    boko_trace_event_t event;

    boko_trace_init_event(&event, BOKO_TAG_HEARTBEAT);
    event.seq = (uint32_t)seq;
    event.p0 = pad1;
    event.p1 = pad2;
    event.p2 = sys;
    event.pc = pc;
    event.scanline = (uint16_t)scanline;
    event.a = a;
    event.r2c = r2c;
    event.r3b = r3b;
    event.r3c = r3c;
    event.r9d = r9d;
    boko_trace_put_event(&event);
}

void boko_flash_trace_record_freeze(unsigned samples,
                                    WORD pc,
                                    BYTE r2c,
                                    BYTE r3b,
                                    BYTE r3c,
                                    BYTE r9d)
{
    boko_trace_event_t event;

    boko_trace_init_event(&event, BOKO_TAG_FREEZE);
    event.seq = samples;
    event.pc = pc;
    event.r2c = r2c;
    event.r3b = r3b;
    event.r3c = r3c;
    event.r9d = r9d;
    boko_trace_put_event(&event);
    boko_flash_trace_flush();
}

void boko_flash_trace_dump_to_sd(const char *reason)
{
    boko_trace_event_t end_event;
    FIL file;
    FRESULT fr;
    UINT written;
    char path[64];
    const BYTE *src = boko_trace_flash_ptr();
    uint32_t dumped = 0;
    uint32_t total_bytes;

    if (!s_ready || s_dumped) {
        return;
    }

    boko_trace_init_event(&end_event, BOKO_TAG_END);
    end_event.p0 = s_event_count;
    end_event.p1 = s_committed_bytes;
    end_event.p2 = s_page_used;
    boko_trace_put_event(&end_event);
    boko_flash_trace_flush();

    total_bytes = s_committed_bytes;

    printf("[TRACE_SD_BEGIN] reason=%s bytes=%lu events=%lu\r\n",
           reason ? reason : "(null)",
           (unsigned long)total_bytes,
           (unsigned long)s_event_count);
    fflush(stdout);

    if (!boko_trace_build_path(path, sizeof(path))) {
        printf("[TRACE_SD_END] ok=0 path=(none) bytes=0 fr=-1\r\n");
        fflush(stdout);
        return;
    }

    fr = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        printf("[TRACE_SD_END] ok=0 path=%s bytes=0 fr=%d\r\n", path, fr);
        fflush(stdout);
        return;
    }

    while (dumped < total_bytes) {
        const UINT chunk = (UINT)(((total_bytes - dumped) > BOKO_TRACE_PAGE_BYTES)
                                      ? BOKO_TRACE_PAGE_BYTES
                                      : (total_bytes - dumped));
        fr = f_write(&file, src + dumped, chunk, &written);
        if (fr != FR_OK || written != chunk) {
            f_close(&file);
            printf("[TRACE_SD_END] ok=0 path=%s bytes=%lu fr=%d written=%u\r\n",
                   path,
                   (unsigned long)dumped,
                   fr,
                   (unsigned)written);
            fflush(stdout);
            return;
        }
        dumped += chunk;
    }

    fr = f_close(&file);
    s_dumped = true;
    printf("[TRACE_SD_END] ok=%d path=%s bytes=%lu events=%lu fr=%d\r\n",
           fr == FR_OK ? 1 : 0,
           path,
           (unsigned long)dumped,
           (unsigned long)s_event_count,
           fr);
    fflush(stdout);
}
