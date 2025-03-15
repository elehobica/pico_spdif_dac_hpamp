cmake_minimum_required(VERSION 3.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)

set(project_name "spdif_dac_hpamp" C CXX ASM)
project(${project_name})
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(lib/pico_audio_i2s_32b)
add_subdirectory(lib/pico_audio_i2s_32b/src/pico_audio_32b)
add_subdirectory(lib/pico_flash_param)
add_subdirectory(lib/pico_spdif_rx pico_spdif_rx)

# select sample application here (choose only one)
add_executable(${PROJECT_NAME}
    main.cpp
    RotaryEncoder.cpp
)

target_link_libraries(${PROJECT_NAME}
    hardware_exception
    pico_stdlib
    pico_audio_32b
    pico_audio_i2s_32b
    pico_flash_param
    pico_spdif_rx
)

# enable usb output, disable uart output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# set core1 process i2s_callback
# set project resource assignment for PIO and DMA_IRQ to avoid confliction
target_compile_definitions(${PROJECT_NAME} PRIVATE
    CORE1_PROCESS_I2S_CALLBACK
    PICO_SPDIF_RX_PIO=0
    PICO_SPDIF_RX_DMA_IRQ=0
    PICO_AUDIO_I2S_PIO=1
    PICO_AUDIO_I2S_DMA_IRQ=1
    PICO_MAX_SHARED_IRQ_HANDLERS=5
    PICO_FLASH_ASSUME_CORE1_SAFE
)

pico_add_extra_outputs(${PROJECT_NAME}) # create map/bin/hex/uf2 files
