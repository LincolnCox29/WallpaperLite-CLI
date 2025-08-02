cmake_minimum_required(VERSION 3.10)

project(wallpaperLite-CLI C)

set(CMAKE_C_STANDARD 11)

# Explicitly specify MSVC compiler (cl.exe)
set(CMAKE_C_COMPILER "cl")

# Hard configure BUILD_TYPE
set(CMAKE_BUILD_TYPE "Release")

# Hard configure Architecture
set(CMAKE_VS_PLATFORM_NAME "Win32")
message(STATUS "Build architecture: ${CMAKE_VS_PLATFORM_NAME}")

# Set release dir
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")

# Create the output directory if it doesnt exist
file(MAKE_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE}")

# Add compiler flags (-Wall for warnings, -Wextra for extra warnings)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4 /WX")

add_executable(wallpaperLite-CLI wallpaperLite-CLI.c)

# Set path to VLCmin directory
set(VLC_MIN_DIR "${CMAKE_SOURCE_DIR}/VLCmin")

# Verify VLCmin directory exists before copying
if(NOT EXISTS "${VLC_MIN_DIR}")
    message(FATAL_ERROR "VLCmin directory not found at: ${VLC_MIN_DIR}")
endif()

# Copying VLCmin from repo to release directory
message(STATUS "Copying VLCmin to Release directory...")
file(COPY "${VLC_MIN_DIR}" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE}")
