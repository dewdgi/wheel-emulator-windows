#include "input.h"
#include "gamepad.h"

void notify_all_shutdown(Input& input, GamepadDevice& gamepad);

#include <iostream>
#include <atomic>
#include <csignal>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <vector>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <chrono>
#include "config.h"
#include "input.h"
#include "gamepad.h"

std::atomic<bool> running{true};

void signal_handler(int signal) {
    if (signal == SIGINT) {
        const char msg[] = "\n[signal_handler] Received Ctrl+C, shutting down...\n";
        ssize_t ignored = write(STDERR_FILENO, msg, sizeof(msg) - 1);
        (void)ignored;
        running.store(false, std::memory_order_relaxed);
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

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array) ((array[LONG(bit)] >> OFF(bit)) & 1)

int run_detection_mode() {
    // ...existing code for run_detection_mode...
    return 0;
}

// --- main() at very end of file ---
int main(int, char*[]) {
    if (!check_root()) {
        return 1;
    }

    // Setup signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);

    // Load configuration
    Config config;
    config.Load();

    GamepadDevice gamepad;
    if (!gamepad.Create()) {
        std::cerr << "Failed to create virtual gamepad" << std::endl;
        return 1;
    }
    std::cout << "[DEBUG][main] After gamepad.Create(), ffb_thread joinable=" << gamepad.IsFFBThreadJoinable() << std::endl;

    // Discover input devices
    Input input;
    std::cout << "[DEBUG][main] Calling input.DiscoverKeyboard with '" << config.keyboard_device << "'" << std::endl;
    bool kbd_ok = input.DiscoverKeyboard(config.keyboard_device);
    std::cout << "[DEBUG][main] input.DiscoverKeyboard returned " << kbd_ok << std::endl;
    std::cout << "[DEBUG][main] Calling input.DiscoverMouse with '" << config.mouse_device << "'" << std::endl;
    bool mouse_ok = input.DiscoverMouse(config.mouse_device);
    std::cout << "[DEBUG][main] input.DiscoverMouse returned " << mouse_ok << std::endl;

    if (input.get_kbd_fd() < 0 && input.get_mouse_fd() < 0) {
        std::cerr << "No input devices available, exiting." << std::endl;
        return 1;
    }

    while (running) {
        struct pollfd pfds[2];
        int nfds = 0;
        if (input.get_kbd_fd() >= 0) {
            pfds[nfds].fd = input.get_kbd_fd();
            pfds[nfds].events = POLLIN;
            nfds++;
        }
        if (input.get_mouse_fd() >= 0) {
            pfds[nfds].fd = input.get_mouse_fd();
            pfds[nfds].events = POLLIN;
            nfds++;
        }

        int pret = poll(pfds, nfds, -1);
        if (pret < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "[main] poll() error: " << strerror(errno) << std::endl;
            break;
        }

        int mouse_dx = 0;
        input.Read(mouse_dx);

        if (input.CheckToggle()) {
            gamepad.ToggleEnabled(input);
        }

        if (gamepad.IsEnabled()) {
            gamepad.UpdateSteering(mouse_dx, config.sensitivity);
            gamepad.UpdateThrottle(input.IsKeyPressed(KEY_W));
            gamepad.UpdateBrake(input.IsKeyPressed(KEY_S));
            gamepad.UpdateClutch(input.IsKeyPressed(KEY_A));
            gamepad.UpdateButtons(input);
            gamepad.UpdateDPad(input);
            gamepad.SendState();
        }

        gamepad.ProcessUHIDEvents();
    }
    // On shutdown, notify all threads to wake up and exit
    notify_all_shutdown(input, gamepad);
    std::cout << "[DEBUG][main] Main loop exited, running=" << running << std::endl;
    // Signal threads to exit before destruction
    std::cout << "[DEBUG][main] Calling gamepad.ShutdownThreads()" << std::endl;
    gamepad.ShutdownThreads();
    std::cout << "[DEBUG][main] Called input.Grab(false)" << std::endl;
    input.Grab(false);
    std::cout << "[DEBUG][main] Goodbye!" << std::endl;
    return 0;

}

void notify_all_shutdown(Input& input, GamepadDevice& gamepad) {
    // Wake all threads waiting on condition variables
    input.input_cv.notify_all();
    gamepad.NotifyAllShutdownCVs();
}
