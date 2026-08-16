cmake_minimum_required(VERSION 3.18)

# -------------------------------------------------------------------------
# Picocalc_NESco — NES emulator for RP2040 / PicoCalc
# MIT License
# -------------------------------------------------------------------------

# Pull in the Pico SDK (set PICO_SDK_PATH in environment or cmake -DPICO_SDK_PATH=...)
if(DEFINED ENV{PICO_SDK_PATH} AND NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()

if(DEFINED PICO_SDK_PATH)
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
    set(PICO_BUILD ON)
else()
    set(PICO_BUILD OFF)
endif()

project(Picocalc_NESco C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(NESCO_RUNTIME_LOGS "Enable verbose runtime logging on device" OFF)
option(NESCO_INPUT_IO_LOGS "Enable focused $4016/$4017 input I/O logging" OFF)
option(NESCO_BOKOSUKA_STATE_LOGS "Enable focused Bokosuka state logging" OFF)
option(NESCO_CORE1_BASELINE_LOG "Enable one-second baseline performance logs for core1 planning" OFF)
option(NESCO_BG_TILE_SHARE_LOG "Enable low-overhead background tile share measurement" OFF)
option(NESCO_PALETTE_SNAPSHOT_LOG "Enable palette snapshot protocol verification logs" OFF)
option(NESCO_SPRITE_ACTIVE_LIST_METRICS "Enable one-second sprite active-list metrics" OFF)
option(NESCO_SPRITE_ACTIVE_LIST "Use the precomputed per-scanline sprite active list" ON)
option(NESCO_MAPPER4_FRACTIONAL_TIMING
       "Use NTSC 341-dot fractional timing for Mapper 4" ON)
option(NESCO_MAPPER4_A12_SINGLE_SOURCE
       "Use the Mapper 4 A12 edge model instead of tile-callback double clocking" ON)
option(NESCO_MAPPER19_IRQ_DIAGNOSTICS
       "Enable one-shot RAM diagnostics for the local Mapper 19 IRQ fixture" OFF)
option(NESCO_MAPPER19_N163_MUTE_DIAGNOSTIC
       "Mute N163 expansion audio at the final mixer for Mapper 19 diagnosis" OFF)
option(NESCO_MAPPER19_N163_ONLY_DIAGNOSTIC
       "Route only the raw Mapper 19 N163 level to the audio ring for generator comparison" OFF)
option(NESCO_MAPPER19_N163_AVERAGE_FAST
       "Use exact constant-divisor fast paths for the Mapper 19 N163 channel average" OFF)
option(NESCO_MAPPER19_N163_SKIP_SAME_OUTPUT
       "Skip Mapper 19 N163 average recomputation when a channel output is unchanged" OFF)
option(NESCO_MAPPER19_N163_INCREMENTAL_SUM
       "Maintain the Mapper 19 N163 active-channel sum by channel delta" OFF)
option(NESCO_MAPPER19_N163_CACHE_CHANNEL_COUNT
       "Cache the Mapper 19 N163 active-channel count outside the audio hot path" OFF)
option(NESCO_AUDIO_HSYNC_BATCH
       "Batch Mapper 19 HSync audio into 16-sample output calls" OFF)
option(NESCO_AUDIO_HSYNC_BATCH32
       "Batch Mapper 19 HSync audio into 32-sample output calls" OFF)
option(NESCO_AUDIO_PREFILL
       "Prefill the audio ring before enabling the ROM audio stream" OFF)
option(NESCO_AUDIO_RING_8192
       "Use an 8192-sample audio ring for producer jitter absorption" OFF)
option(NESCO_AUDIO_RAMFUNC
       "Place audio producer and DMA refill hot paths in RAM" OFF)
option(NESCO_AUDIO_MIX_RAMFUNC
       "Place only the audio mixer producer function in RAM" OFF)
option(NESCO_AUDIO_DMA_REFILL_RAMFUNC
       "Place only the PWM DMA refill function in RAM" OFF)
option(NESCO_AUDIO_DMA64
       "Use 64 samples per PWM DMA half-buffer" OFF)
option(NESCO_AUDIO_HIGH_PRIORITY
       "Give the PWM audio DMA channels RP2040 high-priority arbitration" OFF)
option(NESCO_AUDIO_DYNAMIC_PRIORITY
       "Raise PWM audio DMA priority only while the audio ring is below its low watermark" OFF)
option(NESCO_LCD_DMA_STRIP4
       "Use four source lines per LCD DMA burst for audio/display arbitration" OFF)
option(NESCO_AUDIO_DISPLAY_BACKOFF
       "Skip a display frame while the audio ring is below its low watermark" OFF)
option(NESCO_AUDIO_LCD_RING_GUARD
       "Defer LCD strip DMA briefly while the audio ring is below its low watermark" OFF)
option(NESCO_DIAGNOSTIC_AUTOSTART_BUILTIN
       "Diagnostic-only direct launch of the staged builtin ROM; default OFF" OFF)

# Probe slightly larger linker-reserved heap without modifying the Pico SDK.
# Disable malloc panic so heap diagnostics can measure allocation ceilings safely.
add_compile_definitions(
    PICO_HEAP_SIZE=0x100
    PICO_MALLOC_PANIC=0
)

if(PICO_BUILD)
    pico_sdk_init()
endif()

# -------------------------------------------------------------------------
# Platform sources
# -------------------------------------------------------------------------
set(PLATFORM_SOURCES
    platform/main.c
    platform/core1_worker.c
    platform/infones_session.cpp
    platform/display.c
    platform/audio.c
    platform/input.c
    platform/input_lcd_test.c
    platform/rom_menu.c
    platform/rom_image.c
    platform/screenshot.c
    platform/screenshot_storage.c
    platform/screenshot_viewer.c
    platform/sram_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fatfs/ff.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fatfs/ffunicode.c
    drivers/lcd_spi.c
    drivers/fatfs_diskio.c
    drivers/pwm_audio.c
    drivers/i2c_kbd.c
    drivers/sdcard.c
)

if(NESCO_BOKOSUKA_STATE_LOGS)
    list(APPEND PLATFORM_SOURCES
        drivers/boko_flash_trace.c
    )
endif()

set_source_files_properties(
    platform/display.c
    platform/audio.c
    platform/input.c
    platform/rom_image.c
    platform/screenshot.c
    platform/screenshot_storage.c
    PROPERTIES LANGUAGE CXX
)

if(NOT PICO_BUILD)
    message(FATAL_ERROR "PICO_SDK_PATH is required. Host build is disabled during the InfoNES migration.")
endif()

add_subdirectory(infones)

# -------------------------------------------------------------------------
# Target: Picocalc_NESco.elf  (RP2040 firmware)
# -------------------------------------------------------------------------
if(PICO_BUILD)
    add_executable(Picocalc_NESco
        ${PLATFORM_SOURCES}
    )

    pico_generate_pio_header(Picocalc_NESco
        ${CMAKE_CURRENT_SOURCE_DIR}/drivers/lcd_spi.pio
    )

    target_include_directories(Picocalc_NESco PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/infones
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
        ${CMAKE_CURRENT_SOURCE_DIR}/drivers
        ${CMAKE_CURRENT_SOURCE_DIR}/fatfs
    )

    target_compile_definitions(Picocalc_NESco PRIVATE
        PICO_BUILD=1
    )

    if(NESCO_RUNTIME_LOGS)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_RUNTIME_LOGS=1
        )
    endif()

    if(NESCO_CORE1_BASELINE_LOG OR NESCO_BG_TILE_SHARE_LOG OR NESCO_PALETTE_SNAPSHOT_LOG)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_CORE1_BASELINE_LOG=1
        )
    endif()

    if(NESCO_BG_TILE_SHARE_LOG)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_BG_TILE_SHARE_LOG=1
        )
    endif()

    if(NESCO_PALETTE_SNAPSHOT_LOG)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_PALETTE_SNAPSHOT_LOG=1
        )
    endif()

    if(NESCO_MAPPER19_IRQ_DIAGNOSTICS)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_MAPPER19_IRQ_DIAGNOSTICS=1
        )
    endif()

    if(NESCO_MAPPER19_N163_MUTE_DIAGNOSTIC)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_MAPPER19_N163_MUTE_DIAGNOSTIC=1
        )
    endif()

    if(NESCO_MAPPER19_N163_ONLY_DIAGNOSTIC)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_MAPPER19_N163_ONLY_DIAGNOSTIC=1
        )
    endif()

    if(NESCO_MAPPER19_N163_AVERAGE_FAST)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_MAPPER19_N163_AVERAGE_FAST=1
        )
    endif()

    if(NESCO_MAPPER19_N163_SKIP_SAME_OUTPUT)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_MAPPER19_N163_SKIP_SAME_OUTPUT=1
        )
    endif()

    if(NESCO_MAPPER19_N163_INCREMENTAL_SUM)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_MAPPER19_N163_INCREMENTAL_SUM=1
        )
    endif()

    if(NESCO_MAPPER19_N163_CACHE_CHANNEL_COUNT)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_MAPPER19_N163_CACHE_CHANNEL_COUNT=1
        )
    endif()

    if(NESCO_AUDIO_HSYNC_BATCH)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_AUDIO_HSYNC_BATCH=1
        )
    endif()

    if(NESCO_AUDIO_HSYNC_BATCH32)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_AUDIO_HSYNC_BATCH=1
            NESCO_AUDIO_HSYNC_BATCH32=1
        )
    endif()

    if(NESCO_AUDIO_PREFILL)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_AUDIO_PREFILL=1
        )
    endif()

    if(NESCO_AUDIO_RING_8192)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_AUDIO_RING_8192=1
        )
    endif()

    if(NESCO_AUDIO_RAMFUNC)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_AUDIO_RAMFUNC=1
        )
    endif()

    if(NESCO_AUDIO_MIX_RAMFUNC)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_AUDIO_MIX_RAMFUNC=1
        )
    endif()

    if(NESCO_AUDIO_DMA_REFILL_RAMFUNC)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_AUDIO_DMA_REFILL_RAMFUNC=1
        )
    endif()

    if(NESCO_AUDIO_DMA64)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_AUDIO_DMA64=1
        )
    endif()

    if(NESCO_AUDIO_HIGH_PRIORITY)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_AUDIO_HIGH_PRIORITY=1
        )
    endif()

    if(NESCO_AUDIO_DYNAMIC_PRIORITY)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_AUDIO_DYNAMIC_PRIORITY=1
        )
    endif()

    if(NESCO_LCD_DMA_STRIP4)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_LCD_DMA_STRIP4=1
        )
    endif()

    if(NESCO_AUDIO_DISPLAY_BACKOFF)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_AUDIO_DISPLAY_BACKOFF=1
        )
    endif()

    if(NESCO_AUDIO_LCD_RING_GUARD)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_AUDIO_LCD_RING_GUARD=1
        )
    endif()

    if(NESCO_DIAGNOSTIC_AUTOSTART_BUILTIN)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_DIAGNOSTIC_AUTOSTART_BUILTIN=1
        )
    endif()

    if(NESCO_SPRITE_ACTIVE_LIST_METRICS)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_CORE1_BASELINE_LOG=1
            NESCO_SPRITE_ACTIVE_LIST_METRICS=1
        )
    endif()

    # Optimize: -O3, link-time optimization, place hot functions in RAM
    target_compile_options(Picocalc_NESco PRIVATE
        -O3
        -Wall
        -Wextra
        -Wno-unused-parameter
    )

    target_link_libraries(Picocalc_NESco
        infones
        pico_stdlib
        hardware_clocks
        hardware_dma
        hardware_pio
        hardware_spi
        hardware_i2c
        hardware_pwm
        hardware_flash
        hardware_irq
        pico_multicore
        pico_sync
    )

    # Generate .uf2 for drag-and-drop flashing
    pico_add_extra_outputs(Picocalc_NESco)

    # PicoCalc debug console uses uart0
    pico_enable_stdio_usb(Picocalc_NESco 0)
    pico_enable_stdio_uart(Picocalc_NESco 1)

    add_custom_command(TARGET Picocalc_NESco POST_BUILD
        COMMAND arm-none-eabi-size $<TARGET_FILE:Picocalc_NESco>
        COMMAND /bin/sh -c "strings '$<TARGET_FILE:Picocalc_NESco>' | grep 'PicoCalc NESco Ver\\.' | head -n 1"
        VERBATIM
    )
endif()
