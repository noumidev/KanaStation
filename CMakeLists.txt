# Require CMake 3.16
cmake_minimum_required(VERSION 3.16)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wall)

# Default to release builds with debug info
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

project(KanaStation CXX)

# Add subdirectories
add_subdirectory(${PROJECT_SOURCE_DIR}/external/cryptopp-cmake)
add_subdirectory(${PROJECT_SOURCE_DIR}/external/spdlog)

# Enable assertions in RelWithDebInfo builds
string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

# Set include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Set source files
set(SOURCES
    src/core/config.cpp
    src/core/kanacore.cpp
    src/core/scheduler.cpp
    src/core/hw/audio.cpp
    src/core/hw/boot_rom.cpp
    src/core/hw/bus.cpp
    src/core/hw/ddr_ram.cpp
    src/core/hw/dmacplus.cpp
    src/core/hw/edram.cpp
    src/core/hw/gpio.cpp
    src/core/hw/i2c.cpp
    src/core/hw/intc.cpp
    src/core/hw/kirk.cpp
    src/core/hw/ms.cpp
    src/core/hw/nand.cpp
    src/core/hw/shared_ram.cpp
    src/core/hw/spi.cpp
    src/core/hw/spock.cpp
    src/core/hw/syscon.cpp
    src/core/hw/sysctrl.cpp
    src/core/hw/systime.cpp
    src/core/hw/allegrex/allegrex.cpp
    src/core/hw/allegrex/interpreter.cpp
    src/core/hw/allegrex/scratchpad.cpp
    src/core/hw/me/scratchpad.cpp
    src/core/hw/me/vme_dmac.cpp
    src/core/hw/uart/uart.cpp
    src/frontends/sdl/main.cpp
)

# Set header files
set(HEADERS
    include/common/types.hpp
    include/core/config.hpp
    include/core/kanacore.hpp
    include/core/scheduler.hpp
    include/core/hw/audio.hpp
    include/core/hw/boot_rom.hpp
    include/core/hw/bus.hpp
    include/core/hw/ddr_ram.hpp
    include/core/hw/dmacplus.hpp
    include/core/hw/edram.hpp
    include/core/hw/gpio.hpp
    include/core/hw/i2c.hpp
    include/core/hw/intc.hpp
    include/core/hw/kirk.hpp
    include/core/hw/ms.hpp
    include/core/hw/nand.hpp
    include/core/hw/shared_ram.hpp
    include/core/hw/spi.hpp
    include/core/hw/spock.hpp
    include/core/hw/syscon.hpp
    include/core/hw/sysctrl.hpp
    include/core/hw/systime.hpp
    include/core/hw/allegrex/allegrex.hpp
    include/core/hw/allegrex/interpreter.hpp
    include/core/hw/allegrex/scratchpad.hpp
    include/core/hw/me/scratchpad.hpp
    include/core/hw/me/vme_dmac.hpp
    include/core/hw/uart/uart.hpp
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} PRIVATE cryptopp spdlog)
