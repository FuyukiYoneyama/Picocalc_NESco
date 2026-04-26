#include "ff.h"
#include "diskio.h"
#include "sdcard.h"

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) {
        return STA_NOINIT;
    }
    return sdcard_init() ? 0 : (STA_NOINIT | (sdcard_is_present() ? 0 : STA_NODISK));
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0) {
        return STA_NOINIT;
    }
    if (!sdcard_is_present()) {
        return STA_NODISK | STA_NOINIT;
    }
    return sdcard_is_initialized() ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || !buff || count == 0) {
        return RES_PARERR;
    }
    return sdcard_read_sectors((uint32_t)sector, buff, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || !buff || count == 0) {
        return RES_PARERR;
    }
    return sdcard_write_sectors((uint32_t)sector, buff, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    uint32_t sector_count = 0;

    if (pdrv != 0) {
        return RES_PARERR;
    }

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        if (!buff || !sdcard_get_sector_count(&sector_count)) {
            return RES_ERROR;
        }
        *(DWORD *)buff = sector_count;
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (!buff) {
            return RES_PARERR;
        }
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        if (!buff) {
            return RES_PARERR;
        }
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
