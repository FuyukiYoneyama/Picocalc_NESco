#include "sdcard.h"
#include "runtime_log.h"

#ifdef PICO_BUILD
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <stdio.h>
#endif

enum {
    SD_PIN_SCK = 18,
    SD_PIN_MOSI = 19,
    SD_PIN_MISO = 16,
    SD_PIN_CS = 17,
    SD_PIN_DET = 22,
    SD_BLOCK_SIZE = 512,
    SD_CMD24 = 24,
};

#ifdef PICO_BUILD
static spi_inst_t *const SD_SPI = spi0;
static bool s_sd_initialized = false;
static bool s_sd_sdhc = false;
static bool s_sd_present_valid = false;
static bool s_sd_present_cached = false;
static bool s_sd_det_gpio_ready = false;

static uint8_t sd_spi_xfer(uint8_t value);

static void sd_detect_gpio_init(void) {
    if (s_sd_det_gpio_ready) {
        return;
    }
    gpio_init(SD_PIN_DET);
    gpio_set_dir(SD_PIN_DET, GPIO_IN);
    gpio_pull_up(SD_PIN_DET);
    s_sd_det_gpio_ready = true;
}

static bool sdcard_poll_present(void) {
    sd_detect_gpio_init();
    s_sd_present_cached = !gpio_get(SD_PIN_DET);
    s_sd_present_valid = true;
    NESCO_LOGF("[SD] detect gpio22=%d present=%d\r\n", gpio_get(SD_PIN_DET), s_sd_present_cached ? 1 : 0);
    return s_sd_present_cached;
}

static void sd_select(void) {
    gpio_put(SD_PIN_CS, 0);
    (void)sd_spi_xfer(0xFF);
}

static void sd_deselect(void) {
    gpio_put(SD_PIN_CS, 1);
    (void)sd_spi_xfer(0xFF);
}

static uint8_t sd_spi_xfer(uint8_t value) {
    uint8_t rx = 0xFF;
    spi_write_read_blocking(SD_SPI, &value, &rx, 1);
    return rx;
}

static void sd_spi_clock_idle(int count) {
    while (count-- > 0) {
        (void)sd_spi_xfer(0xFF);
    }
}

static bool sd_wait_ready(uint32_t timeout_ms) {
    absolute_time_t until = make_timeout_time_ms((int)timeout_ms);
    while (absolute_time_diff_us(get_absolute_time(), until) > 0) {
        if (sd_spi_xfer(0xFF) == 0xFF) {
            return true;
        }
    }
    return false;
}

static uint8_t sd_command(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *extra, int extra_len) {
    uint8_t response = 0xFF;

    sd_deselect();
    if (!sd_wait_ready(20)) {
        return 0xFF;
    }

    sd_select();
    if (!sd_wait_ready(20)) {
        sd_deselect();
        return 0xFF;
    }

    sd_spi_xfer((uint8_t)(0x40u | cmd));
    sd_spi_xfer((uint8_t)(arg >> 24));
    sd_spi_xfer((uint8_t)(arg >> 16));
    sd_spi_xfer((uint8_t)(arg >> 8));
    sd_spi_xfer((uint8_t)arg);
    sd_spi_xfer(crc);

    for (int i = 0; i < 10; i++) {
        response = sd_spi_xfer(0xFF);
        if ((response & 0x80u) == 0) {
            break;
        }
    }

    for (int i = 0; i < extra_len; i++) {
        extra[i] = sd_spi_xfer(0xFF);
    }

    return response;
}

static bool sd_read_data_block(uint8_t *buffer, uint32_t len) {
    uint8_t token = 0xFF;

    for (int i = 0; i < 20000; i++) {
        token = sd_spi_xfer(0xFF);
        if (token == 0xFE) {
            break;
        }
    }
    if (token != 0xFE) {
        return false;
    }

    for (uint32_t i = 0; i < len; i++) {
        buffer[i] = sd_spi_xfer(0xFF);
    }

    (void)sd_spi_xfer(0xFF);
    (void)sd_spi_xfer(0xFF);
    return true;
}

static bool sd_write_data_block(const uint8_t *buffer, uint8_t token) {
    uint8_t response;

    if (!sd_wait_ready(500)) {
        return false;
    }

    sd_spi_xfer(token);
    if (token != 0xFD) {
        for (uint32_t i = 0; i < SD_BLOCK_SIZE; i++) {
            sd_spi_xfer(buffer[i]);
        }
        (void)sd_spi_xfer(0xFF);
        (void)sd_spi_xfer(0xFF);

        response = sd_spi_xfer(0xFF);
        if ((response & 0x1Fu) != 0x05u) {
            return false;
        }
    }
    return true;
}

static bool sd_read_csd(uint8_t csd[16]) {
    uint8_t r1 = sd_command(9, 0, 0x01, NULL, 0);
    if (r1 != 0x00) {
        sd_deselect();
        sd_spi_xfer(0xFF);
        return false;
    }
    if (!sd_read_data_block(csd, 16)) {
        sd_deselect();
        sd_spi_xfer(0xFF);
        return false;
    }
    sd_deselect();
    sd_spi_xfer(0xFF);
    return true;
}

bool sdcard_is_present(void) {
    if (s_sd_present_valid) {
        return s_sd_present_cached;
    }
    return sdcard_poll_present();
}

bool sdcard_init(void) {
    uint8_t r7[4];
    uint8_t ocr[4];

    if (s_sd_initialized) {
        return true;
    }
    if (!sdcard_poll_present()) {
        NESCO_LOGF("[SD] no card detected\r\n");
        return false;
    }

    NESCO_LOGF("[SD] init spi0 sck=18 mosi=19 miso=16 cs=17\r\n");
    spi_init(SD_SPI, 400000u);
    spi_set_format(SD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(SD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MISO, GPIO_FUNC_SPI);
    gpio_pull_up(SD_PIN_MISO);

    gpio_init(SD_PIN_CS);
    gpio_set_dir(SD_PIN_CS, GPIO_OUT);
    sd_deselect();

    sd_deselect();
    sd_spi_clock_idle(10);
    NESCO_LOGF("[SD] sent initial 80 clocks\r\n");

    for (int i = 0; i < 20; i++) {
        uint8_t r1 = sd_command(0, 0, 0x95, NULL, 0);
        sd_deselect();
        sd_spi_xfer(0xFF);
        NESCO_LOGF("[SD] CMD0 try=%d r1=%02X\r\n", i + 1, r1);
        if (r1 == 0x01) {
            break;
        }
        if (i == 19) {
            return false;
        }
        sleep_ms(10);
    }

    {
        uint8_t r1 = sd_command(8, 0x000001AAu, 0x87, r7, 4);
        NESCO_LOGF("[SD] CMD8 r1=%02X r7=%02X %02X %02X %02X\r\n", r1, r7[0], r7[1], r7[2], r7[3]);
        if (r1 != 0x01) {
            sd_deselect();
            sd_spi_xfer(0xFF);
            return false;
        }
    }
    sd_deselect();
    sd_spi_xfer(0xFF);

    if (r7[2] != 0x01 || r7[3] != 0xAA) {
        return false;
    }

    for (int i = 0; i < 200; i++) {
        uint8_t r1 = sd_command(55, 0, 0x01, NULL, 0);
        sd_deselect();
        sd_spi_xfer(0xFF);
        NESCO_LOGF("[SD] CMD55 try=%d r1=%02X\r\n", i + 1, r1);
        if (r1 > 0x01) {
            return false;
        }

        r1 = sd_command(41, 0x40000000u, 0x01, NULL, 0);
        sd_deselect();
        sd_spi_xfer(0xFF);
        NESCO_LOGF("[SD] ACMD41 try=%d r1=%02X\r\n", i + 1, r1);
        if (r1 == 0x00) {
            break;
        }
        if (i == 199) {
            return false;
        }
        sleep_ms(10);
    }

    {
        uint8_t r1 = sd_command(58, 0, 0x01, ocr, 4);
        NESCO_LOGF("[SD] CMD58 r1=%02X ocr=%02X %02X %02X %02X\r\n", r1, ocr[0], ocr[1], ocr[2], ocr[3]);
        if (r1 != 0x00) {
            sd_deselect();
            sd_spi_xfer(0xFF);
            return false;
        }
    }
    sd_deselect();
    sd_spi_xfer(0xFF);

    s_sd_sdhc = (ocr[0] & 0x40u) != 0;
    spi_set_baudrate(SD_SPI, 12000000u);
    NESCO_LOGF("[SD] init ok sdhc=%d baud=12000000\r\n", s_sd_sdhc ? 1 : 0);
    s_sd_initialized = true;
    return true;
}

bool sdcard_is_initialized(void) {
    return s_sd_initialized;
}

bool sdcard_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t count) {
    if (!buffer || count == 0 || !sdcard_init()) {
        return false;
    }

    while (count-- > 0) {
        uint32_t addr = s_sd_sdhc ? lba : (lba * SD_BLOCK_SIZE);
        uint8_t r1 = sd_command(17, addr, 0x01, NULL, 0);
        if (r1 != 0x00 || !sd_read_data_block(buffer, SD_BLOCK_SIZE)) {
            sd_deselect();
            sd_spi_xfer(0xFF);
            return false;
        }
        sd_deselect();
        sd_spi_xfer(0xFF);
        buffer += SD_BLOCK_SIZE;
        lba++;
    }

    return true;
}

bool sdcard_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t count) {
    if (!buffer || count == 0 || !sdcard_init()) {
        return false;
    }

    while (count-- > 0) {
        uint32_t addr = s_sd_sdhc ? lba : (lba * SD_BLOCK_SIZE);
        uint8_t r1 = sd_command(SD_CMD24, addr, 0x01, NULL, 0);
        if (r1 != 0x00 || !sd_write_data_block(buffer, 0xFE) || !sd_wait_ready(500)) {
            sd_deselect();
            sd_spi_xfer(0xFF);
            return false;
        }
        sd_deselect();
        sd_spi_xfer(0xFF);
        buffer += SD_BLOCK_SIZE;
        lba++;
    }

    return true;
}

bool sdcard_get_sector_count(uint32_t *sector_count) {
    uint8_t csd[16];
    uint32_t c_size;

    if (!sector_count || !sdcard_init() || !sd_read_csd(csd)) {
        return false;
    }

    if ((csd[0] >> 6) == 1) {
        c_size = ((uint32_t)(csd[7] & 0x3Fu) << 16)
               | ((uint32_t)csd[8] << 8)
               | (uint32_t)csd[9];
        *sector_count = (c_size + 1u) * 1024u;
        return true;
    }

    {
        uint32_t read_bl_len = (uint32_t)(csd[5] & 0x0Fu);
        uint32_t c_size_mult = (uint32_t)(((csd[9] & 0x03u) << 1) | ((csd[10] >> 7) & 0x01u));
        c_size = ((uint32_t)(csd[6] & 0x03u) << 10)
               | ((uint32_t)csd[7] << 2)
               | ((uint32_t)(csd[8] >> 6) & 0x03u);
        *sector_count = (c_size + 1u) * (1u << (c_size_mult + 2u)) * (1u << read_bl_len) / 512u;
    }
    return true;
}

void sdcard_reset(void) {
    s_sd_initialized = false;
    s_sd_sdhc = false;
    s_sd_present_valid = false;
}

#else
bool sdcard_is_present(void) { return false; }
bool sdcard_init(void) { return false; }
bool sdcard_is_initialized(void) { return false; }
bool sdcard_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t count) {
    (void)lba; (void)buffer; (void)count; return false;
}
bool sdcard_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t count) {
    (void)lba; (void)buffer; (void)count; return false;
}
bool sdcard_get_sector_count(uint32_t *sector_count) {
    (void)sector_count; return false;
}
void sdcard_reset(void) { }
#endif
