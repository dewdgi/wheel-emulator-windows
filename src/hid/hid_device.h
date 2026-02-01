#ifndef HID_DEVICE_H
#define HID_DEVICE_H

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <windows.h>
#include "../vjoy_sdk/inc/public.h"
#include "../vjoy_sdk/inc/vjoyinterface.h"

namespace hid {

class HidDevice {
public:
    HidDevice();
    ~HidDevice();

    bool Initialize();
    void Shutdown();

    // vJoy doesn't use file descriptors, but we return a dummy handle or 0
    int fd() const { return 0; }
    bool IsReady() const;

    void SetNonBlockingMode(bool enabled);
    void ResetEndpoint();

    bool WaitForEndpointReady(int timeout_ms = 1500);
    
    // The core output function
    bool WriteReportBlocking(const std::array<uint8_t, 13>& report);
    bool WriteHIDBlocking(const uint8_t* data, size_t size);

    // Map UDC binding to vJoy Acquisition
    bool BindUDC();
    bool UnbindUDC();
    bool IsUdcBound() const;

    // FFB Callback mechanism for WheelDevice to hook into
    void RegisterFFBCallback(void* callback, void* user_data);

private:
    std::atomic<bool> udc_bound_;
    std::atomic<bool> non_blocking_mode_;
    mutable std::mutex udc_mutex_;
    
    // vJoy Device ID (usually 1)
    UINT vjoy_id_ = 1;
};

}  // namespace hid

#endif  // HID_DEVICE_H
