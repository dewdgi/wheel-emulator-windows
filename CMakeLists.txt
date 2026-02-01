cmake_minimum_required(VERSION 3.10)
project(wheel-hid-emulator)

set(CMAKE_CXX_STANDARD 17)

# Input Sources
set(SOURCES
    src/main.cpp
    src/config.cpp
    src/wheel_device.cpp
    src/vjoy_device.cpp
    src/logging/logger.cpp
    src/input/device_enumerator.cpp
    src/input/device_scanner.cpp
    src/input/input_manager.cpp
)

# Include directories for vJoy SDK
include_directories(src/vjoy_sdk/inc)

# Link vJoy Interface library
link_directories(src/vjoy_sdk/lib)

# Link libraries
set(LIBS vJoyInterface)

add_executable(wheel-emulator ${SOURCES})

target_link_libraries(wheel-emulator ${LIBS})
