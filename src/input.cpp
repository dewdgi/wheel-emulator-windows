#include "input.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/input-event-codes.h>

Input::Input() : kbd_fd(-1), mouse_fd(-1), prev_toggle(false) {
    memset(keys, 0, sizeof(keys));
}

Input::~Input() {
    CloseDevice(kbd_fd);
    CloseDevice(mouse_fd);
}

bool Input::DiscoverKeyboard() {
    DIR* dir = opendir("/dev/input");
    if (!dir) {
        std::cerr << "Failed to open /dev/input" << std::endl;
        return false;
    }
    
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
            kbd_fd = fd;
            std::cout << "Found keyboard: " << name << " at " << path << std::endl;
            closedir(dir);
            return true;
        }
        
        close(fd);
    }
    
    closedir(dir);
    std::cerr << "No keyboard found" << std::endl;
    return false;
}

bool Input::DiscoverMouse() {
    DIR* dir = opendir("/dev/input");
    if (!dir) {
        std::cerr << "Failed to open /dev/input" << std::endl;
        return false;
    }
    
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
        unsigned long rel_bitmask[NLONGS(REL_MAX)] = {0};
        ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel_bitmask)), rel_bitmask);
        
        if (test_bit(REL_X, rel_bitmask)) {
            char name[256] = "Unknown";
            ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            
            mouse_fd = fd;
            std::cout << "Found mouse: " << name << " at " << path << std::endl;
            closedir(dir);
            return true;
        }
        
        close(fd);
    }
    
    closedir(dir);
    std::cerr << "No mouse found" << std::endl;
    return false;
}

void Input::Read(int& mouse_dx) {
    mouse_dx = 0;
    struct input_event ev;
    
    // Read keyboard events
    if (kbd_fd >= 0) {
        while (read(kbd_fd, &ev, sizeof(ev)) > 0) {
            if (ev.type == EV_KEY && ev.code < KEY_MAX) {
                keys[ev.code] = (ev.value != 0);
            }
        }
    }
    
    // Read mouse events
    if (mouse_fd >= 0) {
        while (read(mouse_fd, &ev, sizeof(ev)) > 0) {
            if (ev.type == EV_REL && ev.code == REL_X) {
                mouse_dx += ev.value;
            }
        }
    }
}

bool Input::CheckToggle() {
    bool both = keys[KEY_LEFTCTRL] && keys[KEY_M];
    bool toggled = false;
    
    if (both && !prev_toggle) {
        toggled = true;
    }
    
    prev_toggle = both;
    return toggled;
}

void Input::Grab(bool enable) {
    int grab = enable ? 1 : 0;
    
    if (kbd_fd >= 0) {
        if (ioctl(kbd_fd, EVIOCGRAB, grab) < 0) {
            std::cerr << "Failed to " << (enable ? "grab" : "ungrab") << " keyboard" << std::endl;
        } else {
            std::cout << (enable ? "Grabbed" : "Ungrabbed") << " keyboard" << std::endl;
        }
    }
    
    if (mouse_fd >= 0) {
        if (ioctl(mouse_fd, EVIOCGRAB, grab) < 0) {
            std::cerr << "Failed to " << (enable ? "grab" : "ungrab") << " mouse" << std::endl;
        } else {
            std::cout << (enable ? "Grabbed" : "Ungrabbed") << " mouse" << std::endl;
        }
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
