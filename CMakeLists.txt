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

    if(NESCO_CORE1_BASELINE_LOG)
        target_compile_definitions(Picocalc_NESco PRIVATE
            NESCO_CORE1_BASELINE_LOG=1
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
