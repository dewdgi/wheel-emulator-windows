@echo off
echo Attempting to build with g++...

where g++ >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: g++ is not found in your PATH. 
    echo Please install MinGW-w64 and add it to your PATH.
    pause
    exit /b 1
)

if not exist "src\vjoy_sdk\inc\vjoyinterface.h" (
    echo Error: vJoy headers not found in src\vjoy_sdk\inc
    exit /b 1
)

g++ -std=c++17 ^
    src/main.cpp ^
    src/config.cpp ^
    src/wheel_device.cpp ^
    src/hid/hid_device.cpp ^
    src/logging/logger.cpp ^
    src/input/device_scanner.cpp ^
    src/input/input_manager.cpp ^
    src/input/device_enumerator.cpp ^
    -I src/vjoy_sdk/inc ^
    -L src/vjoy_sdk/lib ^
    -l vJoyInterface -lwinmm ^
    -o wheel-emulator.exe ^
    -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic

if %errorlevel% neq 0 (
    echo.
    echo Build FAILED!
    echo Ensure g++ is in your PATH and vJoy SDK files are correct.
    echo If linking fails, vJoyInterface.lib might be MSVC-only format.
) else (
    echo.
    echo Build SUCCESS! Run wheel-emulator.exe to start.
)
