#include <iostream>
#include <csignal>
#include <unistd.h>
#include <linux/input-event-codes.h>
#include "config.h"
#include "input.h"
#include "gamepad.h"

// Global flag for clean shutdown
static volatile bool running = true;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\nReceived Ctrl+C, shutting down..." << std::endl;
        running = false;
    }
}

bool check_root() {
    if (geteuid() != 0) {
        std::cerr << "This program must be run as root to access /dev/uinput and grab input devices." << std::endl;
        std::cerr << "Please run with: sudo ./wheel-emulator" << std::endl;
        return false;
    }
    return true;
}

int main() {
    // Check root privileges
    if (!check_root()) {
        return 1;
    }
    
    std::cout << "=== Wheel Emulator ===" << std::endl;
    std::cout << "Transform keyboard+mouse into Xbox 360 gamepad for racing games" << std::endl;
    std::cout << std::endl;
    
    // Setup signal handler
    signal(SIGINT, signal_handler);
    
    // Load configuration
    Config config;
    if (!config.Load()) {
        std::cerr << "Failed to load configuration" << std::endl;
        return 1;
    }
    std::cout << "Sensitivity: " << config.sensitivity << std::endl;
    std::cout << std::endl;
    
    // Create virtual gamepad
    GamepadDevice gamepad;
    if (!gamepad.Create()) {
        std::cerr << "Failed to create virtual gamepad" << std::endl;
        return 1;
    }
    std::cout << std::endl;
    
    // Discover input devices
    Input input;
    if (!input.DiscoverKeyboard()) {
        std::cerr << "Failed to discover keyboard" << std::endl;
        return 1;
    }
    
    if (!input.DiscoverMouse()) {
        std::cerr << "Failed to discover mouse" << std::endl;
        return 1;
    }
    std::cout << std::endl;
    
    std::cout << std::endl;
    std::cout << "Emulation is OFF. Press Ctrl+M to enable." << std::endl;
    std::cout << std::endl;
    
    bool input_enabled = false;
    
    // Main loop
    while (running) {
        int mouse_dx = 0;
        
        // Read input events
        input.Read(mouse_dx);
        
        // Check for toggle
        if (input.CheckToggle()) {
            input_enabled = !input_enabled;
            if (input_enabled) {
                std::cout << "Emulation ENABLED" << std::endl;
                input.Grab(true);
            } else {
                std::cout << "Emulation DISABLED" << std::endl;
                input.Grab(false);
            }
        }
        
        if (input_enabled) {
            // Update gamepad state
            gamepad.UpdateSteering(mouse_dx, config.sensitivity);
            gamepad.UpdateThrottle(input.IsKeyPressed(KEY_W));
            gamepad.UpdateBrake(input.IsKeyPressed(KEY_S));
            gamepad.UpdateButtons(input);
            gamepad.UpdateDPad(input);
            gamepad.SendState();
        } else {
            // Send neutral state when disabled
            gamepad.SendNeutral();
        }
        
        // Sleep for 1ms (1000 Hz update rate)
        usleep(1000);
    }
    
    // Cleanup
    std::cout << "Cleaning up..." << std::endl;
    input.Grab(false);
    
    std::cout << "Goodbye!" << std::endl;
    return 0;
}
