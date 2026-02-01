#ifndef VJOY_DEVICE_H
#define VJOY_DEVICE_H

#ifdef _WIN32

#include <windows.h>
#include <atomic>
#include <array>
#include <mutex>
#include "wheel_types.h"

// Forward declaration to avoid pulling in full vJoy headers everywhere
struct vJoyInterfaceScanner;

class VJoyDevice {
public:
    VJoyDevice();
    ~VJoyDevice();

    bool Initialize();
    bool IsReady() const;
    void Shutdown();

    // Mapping method: Update vJoy state
    bool Update(float steering, float throttle, float brake, float clutch, const std::array<uint8_t, static_cast<size_t>(WheelButton::Count)>& buttons);

    // FFB related
    void RegisterFFBCallback(void* callback, void* data);

private:
    UINT dev_id_;
    std::atomic<bool> acquired_;
    bool ffb_supported_;
    
    // Helper to map 0.0-1.0 float ranges or -1.0 to 1.0 to vJoy integers
    long MapAxis(float value, long min, long max);
};

#endif // _WIN32
#endif // VJOY_DEVICE_H
