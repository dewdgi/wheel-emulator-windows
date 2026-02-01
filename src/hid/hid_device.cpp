#include "hid_device.h"
#include <iostream>
#include "../logging/logger.h"

#pragma comment(lib, "vjoyinterface.lib")

namespace hid {

HidDevice::HidDevice() : udc_bound_(false), non_blocking_mode_(true) {}

HidDevice::~HidDevice() {
    Shutdown();
}

bool HidDevice::Initialize() {
    // Initialize vJoy
    if (!vJoyEnabled()) {
        std::cerr << "[vJoy] Driver not enabled or not installed." << std::endl;
        return false;
    }

    // Acquire device
    VjdStat status = GetVJDStatus(vjoy_id_);
    if (status == VJD_STAT_OWN || status == VJD_STAT_FREE) {
        if (!AcquireVJD(vjoy_id_)) {
            std::cerr << "[vJoy] Failed to acquire device " << vjoy_id_ << std::endl;
            return false;
        }
    } else {
        std::cerr << "[vJoy] Device " << vjoy_id_ << " is busy or missing. Status: " << status << std::endl;
        return false;
    }

    udc_bound_ = true;
    ResetVJD(vjoy_id_);
    return true;
}

void HidDevice::Shutdown() {
    if (udc_bound_) {
        RelinquishVJD(vjoy_id_);
        udc_bound_ = false;
    }
}

bool HidDevice::IsReady() const {
    return udc_bound_;
}

void HidDevice::SetNonBlockingMode(bool enabled) {
    non_blocking_mode_ = enabled;
}

void HidDevice::ResetEndpoint() {
    ResetVJD(vjoy_id_);
}

bool HidDevice::WaitForEndpointReady(int timeout_ms) {
    return udc_bound_;
}

bool HidDevice::WriteReportBlocking(const std::array<uint8_t, 13>& report) {
    if (!udc_bound_) return false;

    JOYSTICK_POSITION_V2 iReport;
    iReport.bDevice = (BYTE)vjoy_id_;

    // 1. Steering
    // Reconstruct 16-bit value from bytes 0,1 (Little Endian)
    uint16_t steering_u = static_cast<uint16_t>(report[0]) | (static_cast<uint16_t>(report[1]) << 8);
    // Map 0..65535 (Logitech) to 1..32768 (vJoy)
    iReport.wAxisX = static_cast<LONG>((steering_u / 2) + 1);

    // 2. Pedals
    // Bytes 2,3 (Clutch), 4,5 (Throttle), 6,7 (Brake)
    uint16_t clutch_u = static_cast<uint16_t>(report[2]) | (static_cast<uint16_t>(report[3]) << 8);
    uint16_t throttle_u = static_cast<uint16_t>(report[4]) | (static_cast<uint16_t>(report[5]) << 8);
    uint16_t brake_u = static_cast<uint16_t>(report[6]) | (static_cast<uint16_t>(report[7]) << 8);
    
    // In Linux, these are inverted (65535 - val). 
    // WheelDevice already inverted them. So we just pass them through scaled.
    // vJoy Axis: 1..32768
    iReport.wAxisY = static_cast<LONG>((throttle_u / 2) + 1); // Y often throttle
    iReport.wAxisZ = static_cast<LONG>((brake_u / 2) + 1);    // Z often brake
    iReport.wAxisXRot = static_cast<LONG>((clutch_u / 2) + 1); // Rx often clutch

    // 3. Hat
    // Byte 8
    uint8_t hat = report[8] & 0x0F;
    if (hat == 0x0F) iReport.bHats = -1;
    else iReport.bHats = hat * 4500; // 0->0, 1->4500, etc.

    // 4. Buttons
    // Bytes 9-12 (32-bit little endian)
    uint32_t buttons = static_cast<uint32_t>(report[9]) | 
                       (static_cast<uint32_t>(report[10]) << 8) | 
                       (static_cast<uint32_t>(report[11]) << 16) | 
                       (static_cast<uint32_t>(report[12]) << 24);
    iReport.lButtons = buttons;

    return UpdateVJD(vjoy_id_, (PVOID)&iReport);
}

bool HidDevice::WriteHIDBlocking(const uint8_t* data, size_t size) {
    return false;
}

bool HidDevice::BindUDC() {
    return udc_bound_;
}

bool HidDevice::UnbindUDC() {
    return true;
}

bool HidDevice::IsUdcBound() const {
    return udc_bound_;
}

void HidDevice::RegisterFFBCallback(void* callback, void* user_data) {
     FfbRegisterGenCB((FfbGenCB)callback, user_data);
}

}
