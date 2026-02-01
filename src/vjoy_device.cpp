#ifdef _WIN32
#include "vjoy_device.h"
#include <iostream>
#include <cmath>

// Assuming vJoy SDK headers are available in include path or local directory
// If not, the user needs to provide them.
// Generic include name, might need adjustment based on user setup (e.g., "public.h", "vjoyinterface.h")
#include "public.h"
#include "vjoyinterface.h"

#pragma comment(lib, "vJoyInterface.lib")

VJoyDevice::VJoyDevice() : dev_id_(1), acquired_(false), ffb_supported_(false) {
}

VJoyDevice::~VJoyDevice() {
    Shutdown();
}

bool VJoyDevice::Initialize() {
    // Basic check if vJoy is installed
    if (!vJoyEnabled()) {
        std::cerr << "vJoy driver not enabled or not installed." << std::endl;
        return false;
    }

    // Check status of target device (default ID 1)
    VjdStat status = GetVJDStatus(dev_id_);
    if (status == VJD_STAT_OWN) {
        std::cerr << "vJoy Device " << dev_id_ << " is already owned by this feeder." << std::endl;
    } else if (status == VJD_STAT_FREE) {
        // Great, it's free
    } else if (status == VJD_STAT_BUSY) {
        std::cerr << "vJoy Device " << dev_id_ << " is already owned by another feeder." << std::endl;
        return false;
    } else {
        std::cerr << "vJoy Device " << dev_id_ << " general error." << std::endl;
        return false;
    }

    // Acquire
    if (!AcquireVJD(dev_id_)) {
        std::cerr << "Failed to acquire vJoy Device " << dev_id_ << std::endl;
        return false;
    }
    
    acquired_ = true;
    std::cout << "vJoy Device " << dev_id_ << " acquired successfully." << std::endl;

    // Check for FFB
    if (IsDeviceFfb(dev_id_)) {
        std::cout << "vJoy Device " << dev_id_ << " supports Force Feedback." << std::endl;
        ffb_supported_ = true;
    } else {
        std::cout << "vJoy Device " << dev_id_ << " does NOT support Force Feedback." << std::endl;
    }

    // Reset
    ResetVJD(dev_id_);
    
    return true;
}

void VJoyDevice::Shutdown() {
    if (acquired_) {
        RelinquishVJD(dev_id_);
        acquired_ = false;
    }
}

bool VJoyDevice::IsReady() const {
    return acquired_;
}

long VJoyDevice::MapAxis(float value, long min, long max) {
    // Value is assumed roughly -1.0 to 1.0 or 0.0 to 1.0 depending on usage
    // Logic: (val - src_min) * (dst_range) / (src_range) + dst_min
    // Ignoring complex mapping for now, assuming value is -1.0 to +1.0 for steering
    
    // vJoy standard axis is 0x1 to 0x8000 (1 to 32768) usually, or 0-32767.
    // Let's assume 0-32768 midpoint 16384.
    
    double normalized = (value + 1.0) / 2.0; // 0.0 to 1.0
    double scaled = normalized * (max - min) + min;
    return static_cast<long>(scaled);
}

bool VJoyDevice::Update(float steering, float throttle, float brake, float clutch, const std::array<uint8_t, static_cast<size_t>(WheelButton::Count)>& buttons) {
    if (!acquired_) return false;

    // Map Steering (-1.0 to 1.0) to HID_USAGE_X (0 to 32768)
    long max_val = 32768; 
    long x_axis = MapAxis(steering, 0, max_val);
    SetAxis(x_axis, dev_id_, HID_USAGE_X);

    // Throttle, Brake, Clutch (0.0 to 1.0)
    // NOTE: vJoy axes typically 0-MAX. 
    // Check if pedals should be inverted or not. Assuming 0=Released, 1=Full.
    long y_axis = static_cast<long>(throttle * max_val);
    SetAxis(y_axis, dev_id_, HID_USAGE_Y);

    long z_axis = static_cast<long>(brake * max_val);
    SetAxis(z_axis, dev_id_, HID_USAGE_Z);
    
    long rx_axis = static_cast<long>(clutch * max_val);
    SetAxis(rx_axis, dev_id_, HID_USAGE_RX);

    // Buttons
    // WheelButton is a sparse enum or sequential? definition:
    // South=0, East, West, North, TL, TR, TL2, TR2, Select, Start, ThumbL, ThumbR, Mode, Dead...
    // We iterate through all WheelButton types.
    for (size_t i = 0; i < buttons.size(); ++i) {
        // vJoy buttons are 1-indexed usually
        if (buttons[i] > 0) {
            SetBtn(TRUE, dev_id_, static_cast<UCHAR>(i + 1));
        } else {
            SetBtn(FALSE, dev_id_, static_cast<UCHAR>(i + 1));
        }
    }

    return true;
}

void VJoyDevice::RegisterFFBCallback(void* callback, void* data) {
    if (ffb_supported_ && callback) {
        FfbRegisterGenCB((FfbGenCB)callback, data);
    }
}

#endif // _WIN32
