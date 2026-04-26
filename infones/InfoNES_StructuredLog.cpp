#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "InfoNES.h"
#include "InfoNES_StructuredLog.h"
#include "K6502.h"

#if INFONES_STRUCTURED_LOG_ENABLED

namespace {

constexpr unsigned long kStructuredLogTriggerFrames = 120;
constexpr unsigned long kStructuredLogIgnoreStartBeforeFrame = 120;
constexpr unsigned long kStructuredLogPadFocusFrames = 24;
constexpr unsigned long kStructuredLogSnapshotFrame1 = ~0ul;
constexpr unsigned long kStructuredLogSnapshotFrame2 = ~0ul;
constexpr unsigned long kInitialSequenceMaxFrame = 260;
constexpr size_t kInitialSequenceFrameSlots = kInitialSequenceMaxFrame + 1;

struct InitialSequenceCounts {
    uint16_t r2000;
    uint16_t r2001;
    uint16_t r2005;
    uint16_t r2006;
    uint16_t r2007;
    uint16_t ppu7_pattern;
    uint16_t ppu7_nametable;
    uint16_t ppu7_palette;
};

unsigned long g_structured_log_frame = 0;
unsigned long g_structured_log_nmi_count = 0;
unsigned long g_structured_log_window_remaining = 0;
bool g_prev_start_pressed = false;
bool g_structured_log_start_armed = false;
bool g_structured_log_snapshot1_done = false;
bool g_structured_log_snapshot2_done = false;
bool g_structured_log_scroll_trigger_fired = false;
unsigned long g_structured_log_pad_trigger_frame = ~0ul;
InitialSequenceCounts g_initial_sequence_counts[kInitialSequenceFrameSlots];

bool initial_sequence_summary_frame(unsigned long frame)
{
    switch (frame) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 8:
    case 13:
    case 20:
    case 30:
    case 50:
    case 80:
    case 120:
    case 160:
    case 200:
    case 220:
    case 240:
    case 260:
        return true;
    default:
        return false;
    }
}

uint32_t initial_sequence_ppu_hash(WORD start, size_t length)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; ++i) {
        const WORD addr = static_cast<WORD>((start + i) & 0x3fff);
        const BYTE value = PPUBANK[addr >> 10][addr & 0x03ff];
        hash ^= value;
        hash *= 16777619u;
    }
    return hash;
}

void initial_sequence_print_summary()
{
#if INFONES_ENABLE_INITIAL_SEQUENCE_LOG
    if (g_structured_log_frame > kInitialSequenceMaxFrame ||
        !initial_sequence_summary_frame(g_structured_log_frame)) {
        return;
    }

    const auto& counts = g_initial_sequence_counts[g_structured_log_frame];
    std::printf("[PICO_INIT] f=%lu sl=%u r0=%02X r1=%02X r2=%02X b1=%02X a5=%02X q66=%02X q67=%02X nt=%08lX chr=%08lX c2000=%u c2001=%u c2005=%u c2006=%u ppu7=%u pat=%u ntw=%u pal=%u\n",
                g_structured_log_frame,
                static_cast<unsigned>(PPU_Scanline),
                static_cast<unsigned>(PPU_R0),
                static_cast<unsigned>(PPU_R1),
                static_cast<unsigned>(PPU_R2),
                static_cast<unsigned>(RAM[0x00B1]),
                static_cast<unsigned>(RAM[0x00A5]),
                static_cast<unsigned>(RAM[0x0066]),
                static_cast<unsigned>(RAM[0x0067]),
                static_cast<unsigned long>(initial_sequence_ppu_hash(0x2000, 32 * 12)),
                static_cast<unsigned long>(initial_sequence_ppu_hash(0x0000, 0x2000)),
                static_cast<unsigned>(counts.r2000),
                static_cast<unsigned>(counts.r2001),
                static_cast<unsigned>(counts.r2005),
                static_cast<unsigned>(counts.r2006),
                static_cast<unsigned>(counts.r2007),
                static_cast<unsigned>(counts.ppu7_pattern),
                static_cast<unsigned>(counts.ppu7_nametable),
                static_cast<unsigned>(counts.ppu7_palette));
#endif
}

bool structured_log_initial_a5_focus_frame()
{
    return g_structured_log_frame >= 8 && g_structured_log_frame <= 13;
}

const char *structured_log_initial_a5_exec_name(WORD pc)
{
    switch (pc) {
    case 0x8C9A: return "8C9A_LDA_062D";
    case 0x8CA0: return "8CA0_CMP_01";
    case 0x8CA7: return "8CA7_LDA_C0";
    case 0x8CAB: return "8CAB_STA_061B";
    case 0xC8AC: return "C8AC_LDA_A5";
    case 0xC8B1: return "C8B1_STA_2001";
    case 0xD9D2: return "D9D2_TXA";
    case 0xD9D6: return "D9D6_JSR_EB25";
    case 0xD9D9: return "D9D9_JSR_EC1D";
    case 0xE12D: return "E12D_LDA061B";
    case 0xE130: return "E130_AND80";
    case 0xE137: return "E137_SET_A5_00";
    case 0xE13B: return "E13B_JSR_C854";
    case 0xEB0C: return "EB0C_LDA_0674";
    case 0xEB12: return "EB12_STA_0674";
    case 0xEB15: return "EB15_STA_0674";
    case 0xEB18: return "EB18_LDA_00";
    case 0xEB1A: return "EB1A_STA_061B";
    case 0xEB1D: return "EB1D_STA_0600";
    case 0xEB20: return "EB20_LDA_1E";
    case 0xEB22: return "EB22_STA_A5";
    case 0xEB24: return "EB24_RTS";
    case 0xEB25: return "EB25_LDA_FF";
    case 0xEB35: return "EB35_AXS_F0";
    case 0xEB37: return "EB37_BNE_EB29";
    case 0xEB39: return "EB39_RTS";
    case 0xEB3B: return "EB3B_RTS";
    default: return nullptr;
    }
}

bool structured_log_initial_a5_ram_addr(WORD addr)
{
    return addr == 0x00A5 ||
           addr == 0x00B1 ||
           addr == 0x0600 ||
           addr == 0x061B ||
           addr == 0x062D ||
           addr == 0x0674 ||
           addr == 0x06D2;
}

WORD structured_log_stack_return()
{
    const BYTE lo = RAM[0x0100 + ((SP + 1) & 0xFF)];
    const BYTE hi = RAM[0x0100 + ((SP + 2) & 0xFF)];
    return static_cast<WORD>((static_cast<WORD>(hi) << 8) | lo) + 1;
}

BYTE structured_log_rombank_byte(unsigned bank, WORD addr)
{
    if (bank >= 4 || ROMBANK[bank] == nullptr) {
        return 0xFF;
    }
    return ROMBANK[bank][addr & 0x1FFF];
}

int structured_log_rombank_page(unsigned bank)
{
    if (bank >= 4 || ROM == nullptr || ROMBANK[bank] == nullptr) {
        return -1;
    }
    return static_cast<int>((ROMBANK[bank] - ROM) >> 13);
}

void structured_log_initial_a5_print(const char *tag)
{
    std::printf("[A5INIT] f=%lu sl=%u nmi=%lu %s pc=%04X a=%02X x=%02X y=%02X sp=%02X ret=%04X prg=%u A5=%02X B1=%02X r600=%02X r61B=%02X r62D=%02X r674=%02X r6D2=%02X r66=%02X r67=%02X\n",
                g_structured_log_frame,
                static_cast<unsigned>(PPU_Scanline),
                g_structured_log_nmi_count,
                tag,
                static_cast<unsigned>(PC),
                static_cast<unsigned>(A),
                static_cast<unsigned>(X),
                static_cast<unsigned>(Y),
                static_cast<unsigned>(SP),
                static_cast<unsigned>(structured_log_stack_return()),
                static_cast<unsigned>((ROMBANK0 - ROM) >> 14),
                static_cast<unsigned>(RAM[0x00A5]),
                static_cast<unsigned>(RAM[0x00B1]),
                static_cast<unsigned>(RAM[0x0600]),
                static_cast<unsigned>(RAM[0x061B]),
                static_cast<unsigned>(RAM[0x062D]),
                static_cast<unsigned>(RAM[0x0674]),
                static_cast<unsigned>(RAM[0x06D2]),
                static_cast<unsigned>(RAM[0x0066]),
                static_cast<unsigned>(RAM[0x0067]));
}

BYTE structured_log_read_ppu_byte(WORD addr)
{
    addr &= 0x3FFF;
    return PPUBANK[addr >> 10][addr & 0x03FF];
}

BYTE structured_log_attr_palette(BYTE attr, int row, int col)
{
    const unsigned shift = static_cast<unsigned>(((row & 0x02) << 1) | (col & 0x02));
    return static_cast<BYTE>((attr >> shift) & 0x03);
}

void structured_log_dump_tile_snapshot(const char* tag)
{
#if INFONES_ENABLE_PPU2006_EVT_LOG
    const unsigned bgpt = (PPU_R0 & R0_BG_ADDR) ? 0x1000u : 0x0000u;
    const unsigned sppt = (PPU_R0 & R0_SP_ADDR) ? 0x1000u : 0x0000u;
    std::printf("=== %s F=%lu BGPT=%04X SPPT=%04X BG=%d SP=%d ===\n",
                tag,
                g_structured_log_frame,
                bgpt,
                sppt,
                (PPU_R1 & R1_SHOW_SCR) ? 1 : 0,
                (PPU_R1 & R1_SHOW_SP) ? 1 : 0);

    for (int row = 0; row < 12; ++row) {
        std::printf("NT%02d", row);
        for (int col = 0; col < 32; ++col) {
            const WORD ntAddr = static_cast<WORD>(0x2000 + row * 32 + col);
            std::printf(" %02X", structured_log_read_ppu_byte(ntAddr));
        }
        std::printf("\n");

        std::printf("PL%02d ", row);
        for (int col = 0; col < 32; ++col) {
            const WORD attrAddr = static_cast<WORD>(0x23C0 + ((row >> 2) * 8) + (col >> 2));
            const BYTE attr = structured_log_read_ppu_byte(attrAddr);
            std::printf("%u", static_cast<unsigned>(structured_log_attr_palette(attr, row, col)));
        }
        std::printf("\n");
    }

    auto dumpTile = [&](BYTE tile) {
        std::printf("TILE=%02X ADDR=%04X BYTES=", static_cast<unsigned>(tile), bgpt + tile * 16u);
        for (int i = 0; i < 16; ++i) {
            if (i > 0) {
                std::printf(" ");
            }
            std::printf("%02X", structured_log_read_ppu_byte(static_cast<WORD>(bgpt + tile * 16u + i)));
        }
        std::printf("\n");
    };

    if (g_structured_log_frame == kStructuredLogSnapshotFrame1) {
        dumpTile(0x00);
    }
    if (g_structured_log_frame == kStructuredLogSnapshotFrame2) {
        dumpTile(0x00);
        dumpTile(0x01);
        dumpTile(0x10);
        dumpTile(0x11);
    }
#else
    (void)tag;
#endif
}

} // namespace

bool structured_log_event_enabled()
{
#if INFONES_ENABLE_PPU2006_EVT_LOG
    return g_structured_log_window_remaining > 0;
#else
    return false;
#endif
}

bool structured_log_early_ppu2007_enabled()
{
#if INFONES_ENABLE_PPU2006_EVT_LOG
    return false;  // 1.4i: $2007 logging disabled; focus on $2000/$2001
#else
    return false;
#endif
}

void structured_log_open_scroll_window()
{
#if INFONES_ENABLE_PPU2006_EVT_LOG
    return;  // 1.4t: avoid pre-START logging and hot-path slowdowns.
    if (g_structured_log_scroll_trigger_fired) return;
    if (g_structured_log_frame < kStructuredLogIgnoreStartBeforeFrame) return;
    g_structured_log_scroll_trigger_fired = true;
    g_structured_log_window_remaining = kStructuredLogTriggerFrames;
    std::printf("[TRIG] f=%lu CB0B Y-scroll nonzero trigger fired\n",
                g_structured_log_frame);
#endif
}

void structured_log_note_initial_ppu_write(BYTE reg, WORD addr)
{
#if INFONES_ENABLE_INITIAL_SEQUENCE_LOG
    if (g_structured_log_frame > kInitialSequenceMaxFrame) {
        return;
    }

    auto& counts = g_initial_sequence_counts[g_structured_log_frame];
    switch (reg) {
    case 0:
        ++counts.r2000;
        break;
    case 1:
        ++counts.r2001;
        break;
    case 5:
        ++counts.r2005;
        break;
    case 6:
        ++counts.r2006;
        break;
    case 7:
        ++counts.r2007;
        addr &= 0x3fff;
        if (addr < 0x2000) {
            ++counts.ppu7_pattern;
        } else if (addr < 0x3f00) {
            ++counts.ppu7_nametable;
        } else {
            ++counts.ppu7_palette;
        }
        break;
    default:
        break;
    }
#else
    (void)reg;
    (void)addr;
#endif
}

void structured_log_note_initial_a5_exec(WORD pc)
{
#if INFONES_ENABLE_INITIAL_SEQUENCE_LOG
    if (!structured_log_initial_a5_focus_frame()) {
        return;
    }
    const char *name = structured_log_initial_a5_exec_name(pc);
    if (name == nullptr) {
        return;
    }
    char tag[32];
    std::snprintf(tag, sizeof(tag), "EXEC=%s", name);
    structured_log_initial_a5_print(tag);
#else
    (void)pc;
#endif
}

void structured_log_note_initial_a5_code_bytes(WORD pc, BYTE op0, BYTE op1, BYTE op2)
{
#if INFONES_ENABLE_INITIAL_SEQUENCE_LOG
    if (!structured_log_initial_a5_focus_frame()) {
        return;
    }
    if (pc < 0x8000) {
        return;
    }
    const unsigned bank = (pc - 0x8000) >> 13;
    std::printf("[A5CODE] f=%lu sl=%u nmi=%lu pc=%04X op=%02X%02X%02X rb=%02X%02X%02X p0=%d p1=%d p2=%d p3=%d A5=%02X B1=%02X r600=%02X r61B=%02X r62D=%02X r674=%02X r6D2=%02X\n",
                g_structured_log_frame,
                static_cast<unsigned>(PPU_Scanline),
                g_structured_log_nmi_count,
                static_cast<unsigned>(pc),
                static_cast<unsigned>(op0),
                static_cast<unsigned>(op1),
                static_cast<unsigned>(op2),
                static_cast<unsigned>(structured_log_rombank_byte(bank, pc)),
                static_cast<unsigned>(structured_log_rombank_byte(bank, static_cast<WORD>(pc + 1))),
                static_cast<unsigned>(structured_log_rombank_byte(bank, static_cast<WORD>(pc + 2))),
                structured_log_rombank_page(0),
                structured_log_rombank_page(1),
                structured_log_rombank_page(2),
                structured_log_rombank_page(3),
                static_cast<unsigned>(RAM[0x00A5]),
                static_cast<unsigned>(RAM[0x00B1]),
                static_cast<unsigned>(RAM[0x0600]),
                static_cast<unsigned>(RAM[0x061B]),
                static_cast<unsigned>(RAM[0x062D]),
                static_cast<unsigned>(RAM[0x0674]),
                static_cast<unsigned>(RAM[0x06D2]));
#else
    (void)pc;
    (void)op0;
    (void)op1;
    (void)op2;
#endif
}

void structured_log_note_initial_a5_ram_write(WORD addr, BYTE value)
{
#if INFONES_ENABLE_INITIAL_SEQUENCE_LOG
    if (!structured_log_initial_a5_focus_frame() ||
        !structured_log_initial_a5_ram_addr(addr)) {
        return;
    }
    char tag[24];
    std::snprintf(tag, sizeof(tag), "RAM[%04X]=%02X", static_cast<unsigned>(addr), static_cast<unsigned>(value));
    structured_log_initial_a5_print(tag);
#else
    (void)addr;
    (void)value;
#endif
}

void structured_log_note_initial_a5_ppumask(BYTE value)
{
#if INFONES_ENABLE_INITIAL_SEQUENCE_LOG
    if (!structured_log_initial_a5_focus_frame()) {
        return;
    }
    char tag[20];
    std::snprintf(tag, sizeof(tag), "$2001=%02X", static_cast<unsigned>(value));
    structured_log_initial_a5_print(tag);
#else
    (void)value;
#endif
}

unsigned long structured_log_current_frame()
{
    return g_structured_log_frame;
}

unsigned long structured_log_nmi_count()
{
    return g_structured_log_nmi_count;
}

void structured_log_note_nmi_request()
{
#if INFONES_ENABLE_PPU2006_EVT_LOG
    ++g_structured_log_nmi_count;
#endif
}

bool structured_log_near_pad_trigger()
{
#if INFONES_ENABLE_PPU2006_EVT_LOG
    return g_structured_log_pad_trigger_frame != ~0ul &&
           g_structured_log_frame >= g_structured_log_pad_trigger_frame &&
           g_structured_log_frame < g_structured_log_pad_trigger_frame + kStructuredLogPadFocusFrames;
#else
    return false;
#endif
}

void structured_log_arm_start_trigger()
{
#if INFONES_ENABLE_PPU2006_EVT_LOG
    g_structured_log_start_armed = true;
    g_structured_log_window_remaining = 0;
#endif
}

void structured_log_maybe_begin_2006_window(WORD addr, BYTE latch_flag)
{
#if INFONES_ENABLE_PPU2006_EVT_LOG
    if (!g_structured_log_start_armed) {
        return;
    }
    if (!latch_flag) {
        return;
    }
    if ((addr & 0x3F00u) == 0x3F00u) {
        return;
    }
    g_structured_log_start_armed = false;
    g_structured_log_window_remaining = kStructuredLogTriggerFrames;
#else
    (void)addr;
    (void)latch_flag;
#endif
}

extern "C" void InfoNES_StructuredLogReset(void)
{
    g_structured_log_frame = 0;
    g_structured_log_window_remaining = 0;
    g_prev_start_pressed = false;
    g_structured_log_start_armed = false;
    g_structured_log_snapshot1_done = false;
    g_structured_log_snapshot2_done = false;
    g_structured_log_scroll_trigger_fired = false;
    g_structured_log_pad_trigger_frame = ~0ul;
    g_structured_log_nmi_count = 0;
    std::memset(g_initial_sequence_counts, 0, sizeof(g_initial_sequence_counts));
}

extern "C" void InfoNES_StructuredLogEndFrame(void)
{
    if (g_structured_log_window_remaining > 0) {
        --g_structured_log_window_remaining;
    }
    if (!g_structured_log_snapshot1_done && g_structured_log_frame == kStructuredLogSnapshotFrame1) {
        structured_log_dump_tile_snapshot("EARLY_F4");
        g_structured_log_snapshot1_done = true;
    }
    if (!g_structured_log_snapshot2_done && g_structured_log_frame == kStructuredLogSnapshotFrame2) {
        structured_log_dump_tile_snapshot("EARLY_F13");
        g_structured_log_snapshot2_done = true;
    }
    initial_sequence_print_summary();
    ++g_structured_log_frame;
}

extern "C" void InfoNES_StructuredLogNotePadTrigger(bool transition_pressed,
                                                    BYTE joypad,
                                                    DWORD pad1,
                                                    bool a_pressed,
                                                    bool start_pressed)
{
#if INFONES_ENABLE_PPU2006_EVT_LOG
    if (transition_pressed && !g_prev_start_pressed && g_structured_log_frame >= kStructuredLogIgnoreStartBeforeFrame) {
        g_structured_log_window_remaining = kStructuredLogTriggerFrames;
        g_structured_log_start_armed = false;
        g_structured_log_pad_trigger_frame = g_structured_log_frame;
        std::printf("[PADTRIG] f=%lu joypad=%02X pad1=%08lX a=%u start=%u\n",
                    g_structured_log_frame,
                    static_cast<unsigned>(joypad),
                    static_cast<unsigned long>(pad1),
                    a_pressed ? 1u : 0u,
                    start_pressed ? 1u : 0u);
    }
#else
    (void)joypad;
    (void)pad1;
    (void)a_pressed;
    (void)start_pressed;
#endif
    g_prev_start_pressed = transition_pressed;
}

#endif
