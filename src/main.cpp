#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

#include "config.h"
#include "wheel_device.h"
#include "input/input_manager.h"
#include "logging/logger.h"

int ParseLogLevelFromArgs(int argc, char* argv[]);

std::atomic<bool> running{true};

#ifdef _WIN32
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_CLOSE_EVENT) {
        LOG_INFO("main", "Received Ctrl+C/Close, shutting down...");
        running.store(false, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}
#endif

// --- main() ---
int main(int argc, char* argv[]) {
    int log_level = ParseLogLevelFromArgs(argc, argv);
    logging::InitLogger(log_level);
    LOG_INFO("main", "Starting wheel emulator (log level=" << log_level << ")");

#ifdef _WIN32
    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        LOG_ERROR("main", "Could not set console control handler");
        return 1;
    }
#else
    if (geteuid() != 0) {
        std::cerr << "This program must be run as root." << std::endl;
        return 1;
    }
    // Setup signal handler ...
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = [](int){ running.store(false); };
    sigaction(SIGINT, &sa, NULL);
#endif

    // Load configuration
    Config config;
    config.Load();

    WheelDevice wheel_device;
    wheel_device.SetFFBGain(config.ffb_gain);
    if (!wheel_device.Create()) {
        std::cerr << "Failed to create virtual wheel device" << std::endl;
        return 1;
    }

    InputManager input_manager;
    // On Windows, device paths are likely ignored or handled differently by RawInput,
    // but we pass them in case the implementation uses them for matching.
    if (!input_manager.Initialize(config.keyboard_device, config.mouse_device)) {
        std::cerr << "Failed to initialize input manager" << std::endl;
        // Proceeding anyway might be viable if initialization failure isn't fatal on Windows
    }

    std::cout << "All systems ready. Toggle to enable." << std::endl;

    bool input_enabled = false;
    // When disabled, we should probably center the wheel or stop updates.
    // For now, let's just gate the processing.

    InputFrame frame;
    while (running.load(std::memory_order_relaxed)) {
        // WaitForFrame should be implemented to block or sleep to avoid 100% CPU
        if (!input_manager.WaitForFrame(frame)) {
            if (!running.load(std::memory_order_relaxed)) {
                break;
            }
            continue;
        }

        if (frame.toggle_pressed) {
            input_enabled = !input_enabled;
            std::cout << "Input " << (input_enabled ? "ENABLED" : "DISABLED") << std::endl;
            input_manager.GrabDevices(input_enabled); // Optional: if GrabDevices does anything useful on Windows
        }

        if (input_enabled) {
            // Use sensitivity from config
            int sensitivity = config.sensitivity > 0 ? config.sensitivity : 50;
            
            // Pass to wheel
            wheel_device.ProcessInputFrame(frame, sensitivity);
        }
    }
    
    std::cout << "Shutdown complete." << std::endl;
    return 0;
}

int ParseLogLevelFromArgs(int argc, char* argv[]) {
    int level = 0; // Info default
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0) {
            level = 1; // Debug
        } else if (std::strcmp(argv[i], "-q") == 0) {
            level = -1; // Warn/Error only
        }
    }
    return level;
}
