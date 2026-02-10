# Require CMake 3.16
cmake_minimum_required(VERSION 3.16)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wall -Werror)

# Default to release builds with debug info
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

project(KanaStation CXX)

# Add subdirectories
add_subdirectory(${PROJECT_SOURCE_DIR}/external/spdlog)

# Enable assertions in RelWithDebInfo builds
string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

# Set include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Set source files
set(SOURCES
    src/core/config.cpp
    src/core/kanacore.cpp
    src/core/hw/bus.cpp
    src/frontends/sdl/main.cpp
)

# Set header files
set(HEADERS
    include/common/types.hpp
    include/core/config.hpp
    include/core/kanacore.hpp
    include/core/hw/bus.hpp
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} PRIVATE spdlog)
