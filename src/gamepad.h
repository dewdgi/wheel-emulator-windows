#ifndef GAMEPAD_H
#define GAMEPAD_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Input; // Forward declaration

class GamepadDevice {
public:
    GamepadDevice();
    ~GamepadDevice();
    
    // Create virtual Logitech G29 Racing Wheel
    bool Create();
    
    // Update gamepad state
    void UpdateSteering(int delta, int sensitivity);
    void UpdateThrottle(bool pressed);
    void UpdateBrake(bool pressed);
    void UpdateButtons(const Input& input);
    void UpdateDPad(const Input& input);
    
    // Send state to virtual device
    void SendState();
    void SendNeutral();
    
    // Process UHID events (must be called regularly when using UHID)
    void ProcessUHIDEvents();

private:
    int fd;
    bool use_uhid;
    bool use_gadget;  // USB Gadget mode (proper USB device)
    
    // State
    float steering;
    float throttle;
    float brake;
    std::map<std::string, bool> buttons;
    int8_t dpad_x;
    int8_t dpad_y;
    
    // UHID methods
    bool CreateUHID();
    bool CreateUSBGadget();
    bool CreateUInput();
    void SendUHIDReport();
    std::vector<uint8_t> BuildHIDReport();
    
    // UInput methods (legacy)
    void EmitEvent(uint16_t type, uint16_t code, int32_t value);
    int16_t ClampSteering(int16_t value);
};

#endif // GAMEPAD_H
