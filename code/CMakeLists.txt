# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

# initialize pico-sdk from GIT
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_FETCH_FROM_GIT on)

# pico_sdk_import.cmake is a single file copied from this SDK
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(can_board C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()


# mcp25xxfd library
file(GLOB mcp25xxfdSources ./lib/mcp25xxfd/*.c)
add_library(mcp25xxfd ${mcp25xxfdSources})
target_include_directories(mcp25xxfd PUBLIC ./lib/mcp25xxfd)
target_compile_definitions(mcp25xxfd PUBLIC HOST_RP2040 MCP251863)

target_link_libraries(mcp25xxfd pico_stdlib hardware_spi hardware_gpio)

# main application
file(GLOB cSources ./src/*.c)
add_executable(${PROJECT_NAME} ${cSources})

target_include_directories(${PROJECT_NAME} PUBLIC ./include)

# add pico_stdlib library which aggregates commonly used features
target_link_libraries(${PROJECT_NAME} pico_stdlib hardware_spi hardware_gpio mcp25xxfd)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(${PROJECT_NAME})

# use this to select what to outout over
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 1)