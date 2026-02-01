#ifndef WHEEL_DEVICE_H
#define WHEEL_DEVICE_H

#include <array>
#include <atomic>
#include <mutex>

#include <condition_variable>
#include <thread>
#include <cmath>

#include "vjoy_device.h"
#include "input/wheel_input.h"
#include "wheel_types.h"

class WheelDevice {
public:
    WheelDevice();
    ~WheelDevice();

    // Prevent copying
    WheelDevice(const WheelDevice&) = delete;
    WheelDevice& operator=(const WheelDevice&) = delete;

    bool Create();
    void ShutdownThreads(); // No-op on Windows basically
    void SetFFBGain(float gain);
    
    // Main processing loop called by main.cpp
    void ProcessInputFrame(const InputFrame& frame, int sensitivity);

    // FFB Handler
    void OnFFBPacket(void* packet_data);

private:
    void FFBUpdateThread();
    void ParseFFBCommand(const uint8_t* data, size_t size);
    float ShapeFFBTorque(float raw_force) const;
    bool ApplySteeringLocked();

    VJoyDevice hid_device_;
    
    std::mutex state_mutex;
    std::condition_variable ffb_cv;
    std::thread ffb_thread;
    std::atomic<bool> ffb_running;
    
    // Core state (using Linux branch scale: +/- 32768.0f)
    float user_steering = 0.0f;
    float steering = 0.0f; // Combined
    float throttle = 0.0f;
    float brake = 0.0f;
    float clutch = 0.0f;
    int8_t dpad_x = 0;
    int8_t dpad_y = 0;
    std::array<uint8_t, static_cast<size_t>(WheelButton::Count)> button_states{};

    // FFB State
    float ffb_gain = 1.0f;
    float ffb_offset = 0.0f;
    float ffb_velocity = 0.0f;
    int16_t ffb_force = 0;
    int16_t ffb_autocenter = 1024; // Default center spring
};


#endif // WHEEL_DEVICE_H
