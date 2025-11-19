// Improved toggle: allow either Ctrl key, and tolerate quick presses
#include "input.h"
#include <iostream>
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <linux/input-event-codes.h>
#include <atomic>
extern std::atomic<bool> running;

void Input::Read() {
    int dummy = 0;
    Read(dummy);
}

void Input::NotifyInputChanged() {
    input_cv.notify_all();
}

// Bit manipulation macros for input device capabilities
#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

Input::Input() : kbd_fd(-1), mouse_fd(-1), prev_toggle(false) {
    memset(keys, 0, sizeof(keys));
}

Input::~Input() {
    CloseDevice(kbd_fd);
    CloseDevice(mouse_fd);
}

bool Input::DiscoverKeyboard(const std::string& device_path) {
    // If explicit device path provided, use it
    if (!device_path.empty()) {
        kbd_fd = open(device_path.c_str(), O_RDONLY | O_NONBLOCK);
        if (kbd_fd >= 0) {
            char name[256] = {0};
            ioctl(kbd_fd, EVIOCGNAME(sizeof(name)), name);
        }
        if (kbd_fd < 0) {
            std::cerr << "Failed to open keyboard device: " << device_path << ", errno=" << errno << std::endl;
            return false;
        }
        char name[256] = "Unknown";
        ioctl(kbd_fd, EVIOCGNAME(sizeof(name)), name);
        std::cout << "Using configured keyboard: " << name << " at " << device_path << std::endl;
        return true;
    }
    
    // Otherwise, auto-detect
    DIR* dir = opendir("/dev/input");
    if (!dir) {
        std::cerr << "Failed to open /dev/input" << std::endl;
        return false;
    }
    
    struct KeyboardCandidate {
        std::string path;
        std::string name;
        int priority;
        int fd;
    };
    std::vector<KeyboardCandidate> candidates;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }
        std::string path = std::string("/dev/input/") + entry->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        char name[256] = "Unknown";
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        // Convert to lowercase for case-insensitive comparison
        std::string name_lower = name;
        for (char& c : name_lower) {
            c = tolower(c);
        }
        if (name_lower.find("keyboard") != std::string::npos) {
            int priority = 50; // default
            
            // Deprioritize consumer control and system control
            if (name_lower.find("consumer control") != std::string::npos ||
                name_lower.find("system control") != std::string::npos) {
                priority = 10;
            }
            // Prioritize actual keyboard devices
            else if (name_lower.find(" keyboard") != std::string::npos) {
                priority = 100;
            }
            
            candidates.push_back({path, name, priority, fd});
        } else {
            close(fd);
        }
    }
    
    closedir(dir);
    
    if (candidates.empty()) {
        std::cerr << "No keyboard found" << std::endl;
        return false;
    }
    
    // Sort by priority (highest first)
    std::sort(candidates.begin(), candidates.end(),
              [](const KeyboardCandidate& a, const KeyboardCandidate& b) {
                  return a.priority > b.priority;
              });
    
    // Close all except the best one
    for (size_t i = 1; i < candidates.size(); i++) {
        close(candidates[i].fd);
    }
    
    // Use the highest priority device
    kbd_fd = candidates[0].fd;
    std::cout << "Using keyboard: " << candidates[0].name << " at " << candidates[0].path << std::endl;
    return true;
}

bool Input::DiscoverMouse(const std::string& device_path) {
    // If explicit device path provided, use it
    if (!device_path.empty()) {
        mouse_fd = open(device_path.c_str(), O_RDONLY | O_NONBLOCK);
        if (mouse_fd >= 0) {
            char name[256] = {0};
            ioctl(mouse_fd, EVIOCGNAME(sizeof(name)), name);
        }
        if (mouse_fd < 0) {
            std::cerr << "Failed to open mouse device: " << device_path << ", errno=" << errno << std::endl;
            return false;
        }
        char name[256] = "Unknown";
        ioctl(mouse_fd, EVIOCGNAME(sizeof(name)), name);
        std::cout << "Using mouse: " << name << " at " << device_path << std::endl;
        return true;
    }
    
    // Otherwise, auto-detect
    DIR* dir = opendir("/dev/input");
    if (!dir) {
        std::cerr << "Failed to open /dev/input" << std::endl;
        return false;
    }
    
    struct MouseCandidate {
        std::string path;
        std::string name;
        int priority;
    };
    std::vector<MouseCandidate> candidates;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }
        std::string path = std::string("/dev/input/") + entry->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        // Check for REL_X capability
        unsigned long rel_bitmask[NBITS(REL_MAX)] = {0};
        ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel_bitmask)), rel_bitmask);
        if (test_bit(REL_X, rel_bitmask)) {
            char name[256] = "Unknown";
            ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            std::string name_lower = name;
            for (char& c : name_lower) {
                c = tolower(c);
            }
            // Skip keyboard devices that have pointer capabilities
            if (name_lower.find("keyboard") != std::string::npos) {
                close(fd);
                continue;
            }
            // Prioritize: avoid touchpads and virtual devices
            int priority = 50; // default
            // Deprioritize touchpads
            if (name_lower.find("touchpad") != std::string::npos) {
                priority = 10;
            }
            // Deprioritize virtual/internal mouse devices from touchpads
            else if (name_lower.find("uniw") != std::string::npos || 
                     name_lower.find("elan") != std::string::npos ||
                     name_lower.find("synaptics") != std::string::npos) {
                priority = 20;
            }
            // Deprioritize consumer control / system control devices
            else if (name_lower.find("consumer control") != std::string::npos ||
                     name_lower.find("system control") != std::string::npos) {
                priority = 5;
            }
            // Prioritize real mice - check for "mouse" or "wireless device" in name
            else if (name_lower.find("mouse") != std::string::npos ||
                     (name_lower.find("wireless") != std::string::npos && name_lower.find("device") != std::string::npos) ||
                     name_lower.find("beken") != std::string::npos) {
                priority = 100;
            }
            candidates.push_back({path, name, priority});
        }
        close(fd);
    }
    
    closedir(dir);
    
    if (candidates.empty()) {
        std::cerr << "No mouse found" << std::endl;
        return false;
    }
    
    // Sort by priority (highest first)
    std::sort(candidates.begin(), candidates.end(), 
              [](const MouseCandidate& a, const MouseCandidate& b) {
                  return a.priority > b.priority;
              });
    
    // Use the highest priority device
    auto& best = candidates[0];
    mouse_fd = open(best.path.c_str(), O_RDONLY | O_NONBLOCK);
    if (mouse_fd < 0) {
        std::cerr << "Failed to open mouse: " << best.path << ", errno=" << errno << std::endl;
        return false;
    }
    std::cout << "Using mouse: " << best.name << " at " << best.path << std::endl;
    return true;
}

void Input::Read(int& mouse_dx) {
    if (!running) {
        return;
    }
    mouse_dx = 0;
    auto drain_device = [&](int fd, bool is_keyboard) {
        if (fd < 0) {
            return;
        }
        constexpr int kMaxEventsPerDevice = 256;
        int processed = 0;
        struct input_event ev;
        while (processed < kMaxEventsPerDevice) {
            ssize_t n = read(fd, &ev, sizeof(ev));
            if (n == -1) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                std::cerr << "[Input::Read] fd=" << fd << " read error: " << strerror(errno) << std::endl;
                break;
            }
            if (n == 0) {
                break;
            }
            if (n != sizeof(ev)) {
                std::cerr << "[Input::Read] short read from fd=" << fd << std::endl;
                break;
            }
            processed++;
            if (is_keyboard) {
                if (ev.type == EV_KEY && ev.code < KEY_MAX) {
                    keys[ev.code] = (ev.value != 0);
                }
            } else {
                if (ev.type == EV_REL && ev.code == REL_X) {
                    mouse_dx += ev.value;
                }
            }
        }
    };

    drain_device(kbd_fd, true);
    drain_device(mouse_fd, false);
}

// --- Place these at the end of the file ---

bool Input::CheckToggle() {
    bool ctrl = keys[KEY_LEFTCTRL] || keys[KEY_RIGHTCTRL];
    bool m = keys[KEY_M];
    bool both = ctrl && m;
    bool toggled = false;
    if (both && !prev_toggle) {
        toggled = true;
    }
    prev_toggle = both;
    return toggled;
}

void Input::Grab(bool enable) {
    int grab = enable ? 1 : 0;
    if (kbd_fd >= 0 && fcntl(kbd_fd, F_GETFD) != -1) {
        if (ioctl(kbd_fd, EVIOCGRAB, grab) < 0) {
            if (enable) {
                std::cerr << "Failed to grab keyboard (fd=" << kbd_fd << ") errno=" << errno << std::endl;
            } else if (errno != EINVAL && errno != ENODEV) {
                std::cerr << "Failed to release keyboard (fd=" << kbd_fd << ") errno=" << errno << std::endl;
            }
        } else {
            if (enable) {
                std::cout << "Grabbed keyboard (fd=" << kbd_fd << ")" << std::endl;
            }
        }
    } else if (enable) {
        std::cerr << "Cannot grab keyboard: invalid or closed file descriptor." << std::endl;
    }

    if (mouse_fd >= 0 && fcntl(mouse_fd, F_GETFD) != -1) {
        if (ioctl(mouse_fd, EVIOCGRAB, grab) < 0) {
            if (enable) {
                std::cerr << "Failed to grab mouse (fd=" << mouse_fd << ") errno=" << errno << std::endl;
            } else if (errno != EINVAL && errno != ENODEV) {
                std::cerr << "Failed to release mouse (fd=" << mouse_fd << ") errno=" << errno << std::endl;
            }
        } else {
            if (enable) {
                std::cout << "Grabbed mouse (fd=" << mouse_fd << ")" << std::endl;
            }
        }
    } else if (enable) {
        std::cerr << "Cannot grab mouse: invalid or closed file descriptor." << std::endl;
    }
}
bool Input::IsKeyPressed(int keycode) const {
    if (keycode >= 0 && keycode < KEY_MAX) {
        return keys[keycode];
    }
    return false;
}

bool Input::OpenDevice(const char* path, int& fd) {
    fd = open(path, O_RDONLY | O_NONBLOCK);
    return fd >= 0;
}

void Input::CloseDevice(int& fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}
