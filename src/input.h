#ifndef INPUT_H
#define INPUT_H

#include <linux/input.h>

class Input {
public:
    Input();
    ~Input();
    
    // Discover and open input devices
    bool DiscoverKeyboard();
    bool DiscoverMouse();
    
    // Read events from keyboard and mouse
    void Read(int& mouse_dx);
    
    // Check for Ctrl+M toggle (edge detection)
    bool CheckToggle();
    
    // Grab/ungrab devices for exclusive access
    void Grab(bool enable);
    
    // Check if a key is currently pressed
    bool IsKeyPressed(int keycode) const;

private:
    int kbd_fd;
    int mouse_fd;
    bool keys[KEY_MAX];
    bool prev_toggle;
    
    bool OpenDevice(const char* path, int& fd);
    void CloseDevice(int& fd);
};

#endif // INPUT_H
