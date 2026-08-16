add_library(infones STATIC
    InfoNES_Mapper.cpp
    InfoNES_StructuredLog.cpp
    InfoNES_pAPU.cpp
    InfoNES.cpp
    K6502.cpp
)

target_include_directories(infones
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../platform
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivers
)

target_link_libraries(infones
PUBLIC
    pico_stdlib
)

if(NESCO_INPUT_IO_LOGS)
    target_compile_definitions(infones PUBLIC
        INFONES_ENABLE_INPUT_IO_LOG=1
    )
endif()

if(NESCO_BOKOSUKA_STATE_LOGS)
    target_compile_definitions(infones PUBLIC
        INFONES_ENABLE_BOKOSUKA_STATE_LOG=1
    )
endif()

if(NESCO_RUNTIME_LOGS)
    target_compile_definitions(infones PUBLIC
        NESCO_RUNTIME_LOGS=1
    )
endif()

if(NESCO_CORE1_BASELINE_LOG OR NESCO_BG_TILE_SHARE_LOG OR NESCO_PALETTE_SNAPSHOT_LOG)
    target_compile_definitions(infones PUBLIC
        NESCO_CORE1_BASELINE_LOG=1
    )
endif()

if(NESCO_PALETTE_SNAPSHOT_LOG)
    target_compile_definitions(infones PUBLIC
        NESCO_PALETTE_SNAPSHOT_LOG=1
    )
endif()

if(NESCO_MAPPER19_IRQ_DIAGNOSTICS)
    target_compile_definitions(infones PUBLIC
        NESCO_MAPPER19_IRQ_DIAGNOSTICS=1
    )
endif()

if(NESCO_MAPPER19_N163_ONLY_DIAGNOSTIC)
    target_compile_definitions(infones PUBLIC
        NESCO_MAPPER19_N163_ONLY_DIAGNOSTIC=1
    )
endif()

if(NESCO_MAPPER19_N163_AVERAGE_FAST)
    target_compile_definitions(infones PUBLIC
        NESCO_MAPPER19_N163_AVERAGE_FAST=1
    )
endif()

if(NESCO_AUDIO_HSYNC_BATCH)
    target_compile_definitions(infones PUBLIC
        NESCO_AUDIO_HSYNC_BATCH=1
    )
endif()

if(NESCO_AUDIO_HSYNC_BATCH32)
    target_compile_definitions(infones PUBLIC
        NESCO_AUDIO_HSYNC_BATCH=1
        NESCO_AUDIO_HSYNC_BATCH32=1
    )
endif()

if(NESCO_AUDIO_RAMFUNC)
    target_compile_definitions(infones PUBLIC
        NESCO_AUDIO_RAMFUNC=1
    )
endif()

if(NESCO_AUDIO_DMA64)
    target_compile_definitions(infones PUBLIC
        NESCO_AUDIO_DMA64=1
    )
endif()

if(NESCO_BG_TILE_SHARE_LOG)
    target_compile_definitions(infones PUBLIC
        NESCO_BG_TILE_SHARE_LOG=1
    )
endif()

if(NESCO_SPRITE_ACTIVE_LIST_METRICS)
    target_compile_definitions(infones PUBLIC
        NESCO_CORE1_BASELINE_LOG=1
        NESCO_SPRITE_ACTIVE_LIST_METRICS=1
    )
endif()

if(NESCO_SPRITE_ACTIVE_LIST)
    target_compile_definitions(infones PUBLIC
        NESCO_SPRITE_ACTIVE_LIST=1
    )
endif()

if(NESCO_MAPPER4_FRACTIONAL_TIMING)
    target_compile_definitions(infones PUBLIC
        NESCO_MAPPER4_FRACTIONAL_TIMING=1
    )
endif()

if(NESCO_MAPPER4_A12_SINGLE_SOURCE)
    target_compile_definitions(infones PUBLIC
        NESCO_MAPPER4_A12_SINGLE_SOURCE=1
    )
endif()
