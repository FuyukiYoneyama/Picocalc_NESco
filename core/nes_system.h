/*
 * nes_system.h — Core-to-platform callback interface
 *
 * The emulation core calls these functions to interact with the host
 * platform (LCD, audio, input, ROM storage, frame timing).
 * The platform layer implements every function listed here.
 *
 * Part of Picocalc_NESco
 * MIT License
 */
#pragma once

#include "nes_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =====================================================================
 *  Session life-cycle
 * ===================================================================== */

/**
 * InfoNES_ReadRom() — Load a ROM image from the storage backend.
 *
 * On success: sets ROM, VROM (or NULL for CHR-RAM), NesHeader.
 * Returns 0 on success, -1 on failure.
 */
int InfoNES_ReadRom(const char *path);

/**
 * InfoNES_ReleaseRom() — Release ROM image resources.
 *
 * Called at session end (after SoundClose).
 * Platform should free any heap/Flash-map allocations.
 */
void InfoNES_ReleaseRom(void);

/**
 * InfoNES_Error() — Report a fatal error to the user.
 *
 * Called when an unrecoverable condition occurs (unsupported mapper,
 * ROM parse error, memory allocation failure).
 * Implementation should display the message on LCD, then loop forever.
 */
void InfoNES_Error(const char *fmt, ...);

/* =====================================================================
 *  Per-frame / per-scanline rendering hooks
 * ===================================================================== */

/**
 * InfoNES_PreDrawLine(scanline) — Prepare the scanline pixel buffer.
 *
 * Called before InfoNES_DrawLine() on every visible, non-skipped scanline.
 * The platform should set g_wScanLine to point at the buffer for this line.
 */
void InfoNES_PreDrawLine(int scanline);

/**
 * InfoNES_PostDrawLine(scanline) — Flush the completed scanline to the LCD.
 *
 * Called after InfoNES_DrawLine() on every visible, non-skipped scanline.
 * The platform should convert g_wScanLine (RGB444 × 256) → RGB565 and
 * enqueue the strip for DMA transfer to the LCD.
 */
void InfoNES_PostDrawLine(int scanline);

/**
 * InfoNES_LoadFrame() — Frame boundary hook.
 *
 * Called at VBlank start (scanline 241) before NMI is asserted.
 * The platform computes the frame-skip level and sets g_byFrameSkip.
 * Return value: frame-skip level (0 = render all, 1 = skip 1, 2 = skip 2).
 */
int InfoNES_LoadFrame(void);

/* =====================================================================
 *  Input
 * ===================================================================== */

/**
 * InfoNES_PadState() — Read the joypad state from hardware.
 *
 * Called every VBlank (scanline 241).
 * Must write joypad bits into PAD1_Latch and PAD2_Latch.
 *
 * Standard NES button mapping for PAD1_Latch / PAD2_Latch:
 *   Bit 0  = A       Bit 4  = Up
 *   Bit 1  = B       Bit 5  = Down
 *   Bit 2  = Select  Bit 6  = Left
 *   Bit 3  = Start   Bit 7  = Right
 *   Bit 16 = PAD_SYS_QUIT   (system quit request)
 *   Bit 17 = PAD_SYS_RESET  (soft reset request)
 */
void InfoNES_PadState(DWORD *pdwPad1, DWORD *pdwPad2);

/* =====================================================================
 *  Audio
 * ===================================================================== */

/**
 * InfoNES_SoundOpen(samples_per_sync, clock_per_sync) — Initialise audio output.
 *
 * Called after ROM load before the emulation loop starts.
 * samples_per_sync: how many 8-bit mono samples the APU generates per H-Sync.
 * clock_per_sync:   CPU clocks per H-Sync (≈ STEP_PER_SCANLINE).
 */
void InfoNES_SoundOpen(int samples_per_sync, int clock_per_sync);

/**
 * InfoNES_SoundClose() — Stop audio output.
 *
 * Called before InfoNES_ReleaseRom().
 */
void InfoNES_SoundClose(void);

/**
 * InfoNES_SoundOutput(nch, buf0, buf1, buf2, buf3, buf4) — Mix APU channels.
 *
 * Called by the APU every H-Sync with per-channel sample buffers.
 *   nch:  number of samples in each buffer
 *   buf0: Pulse 1 samples (BYTE)
 *   buf1: Pulse 2 samples (BYTE)
 *   buf2: Triangle samples (BYTE)
 *   buf3: Noise samples (BYTE)
 *   buf4: DPCM samples (BYTE)
 *
 * The platform mixes the five channels, writes 8-bit mono to the ring
 * buffer, and handles DMA transfers to the PWM output.
 */
void InfoNES_SoundOutput(int nch,
                         BYTE *buf0, BYTE *buf1,
                         BYTE *buf2, BYTE *buf3, BYTE *buf4);

/* =====================================================================
 *  Mirroring helper (called by mappers)
 * ===================================================================== */

/**
 * InfoNES_Mirroring(type) — Configure nametable mirroring.
 *
 * Sets PPUBANK[8..11] according to the specified mirroring type.
 * type: MIRROR_HORIZONTAL, MIRROR_VERTICAL, MIRROR_ONESCREEN_A/B,
 *       or MIRROR_FOURSCREEN.
 */
void InfoNES_Mirroring(int type);

/* =====================================================================
 *  CPU interrupt wiring (called by mappers)
 * ===================================================================== */

/**
 * K6502_Set_Int_Wiring(nmi_wiring, irq_wiring) — Set interrupt baseline.
 *
 * NMI is pending when NMI_State != NMI_Wiring.
 * IRQ is pending when IRQ_State != IRQ_Wiring and FLAG_I is clear.
 */
void K6502_Set_Int_Wiring(BYTE nmi_wiring, BYTE irq_wiring);

/* =====================================================================
 *  Screen setup helpers (called by PPU)
 * ===================================================================== */

/**
 * InfoNES_SetupScr() — Called after $2006 second write when rendering is on.
 *
 * Allows the platform (or PPU) to perform any per-frame setup triggered
 * by the PPU address being fully loaded.
 * Minimal implementation: no-op.
 */
void InfoNES_SetupScr(void);

#ifdef __cplusplus
}
#endif
