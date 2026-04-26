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
