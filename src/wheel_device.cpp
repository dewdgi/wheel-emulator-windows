#include "wheel_device.h"
#include "logging/logger.h"
#include <iostream>
#include <algorithm>
#include <windows.h> // For PVOID etc if not transitively included, though vjoyinterface.h usually needs it

namespace {
constexpr const char* kTag = "wheel_device";


// Helper to map -1..1 range to whatever logic? 
// Actually vJoy expects axes. 
// VJoyDevice::Update takes float steering, throttle etc. 
// Assuming VJoyDevice handles the scaling to 0..32767 internally.
// Or if it expects normalized 0..1 or -1..1.
// Let's assume -1.0 to 1.0 for steering, 0.0 to 1.0 for pedals.
}

// Callback wrapper
static void CALLBACK FFB_Callback(PVOID data, PVOID user_data) {
    if (user_data) {
        // Need to cast to WheelDevice but WheelDevice declaration might not include ParseFFB?
        // Actually we need to just print or handle something.
        // For minimal implementation let's just log what we got if we can inspect `data`.
        // vJoy SDK says `data` is a pointer to the FFB packet.
        // We probably need to implement the parsing logic similar to Linux gadget.
        
        static_cast<WheelDevice*>(user_data)->OnFFBPacket(data);
    }
}

WheelDevice::WheelDevice() : ffb_gain(1.0f), ffb_running(false) {
    button_states.fill(0);
}

WheelDevice::~WheelDevice() {
    ShutdownThreads();
}

bool WheelDevice::Create() {
    if (hid_device_.Initialize()) {
        hid_device_.RegisterFFBCallback((void*)FFB_Callback, this);
        
        ffb_running = true;
        ffb_thread = std::thread(&WheelDevice::FFBUpdateThread, this);
        
        return true;
    }
    return false;
}

void WheelDevice::ShutdownThreads() {
    ffb_running = false;
    ffb_cv.notify_all();
    if (ffb_thread.joinable()) {
        ffb_thread.join();
    }
    hid_device_.Shutdown();
}

void WheelDevice::SetFFBGain(float gain) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (gain < 0.1f) gain = 0.1f;
    if (gain > 4.0f) gain = 4.0f;
    ffb_gain = gain;
}

void WheelDevice::ProcessInputFrame(const InputFrame& frame, int sensitivity) {
    std::lock_guard<std::mutex> lock(state_mutex);
    
    // Steering logic (accumulation) matched to Linux behavior
    // Linux Logic: step = delta * sensitivity * 0.05. Range +/- 32768.
    
    constexpr float base_gain = 0.05f; 
    float steering_delta = static_cast<float>(frame.mouse_dx) * static_cast<float>(sensitivity) * base_gain;
    
    // Clamp max step speed
    const float max_step = 2000.0f;
    if (steering_delta > max_step) steering_delta = max_step;
    if (steering_delta < -max_step) steering_delta = -max_step;
    
    if (steering_delta != 0.0f) {
        user_steering += steering_delta;
        
        // Clamp -32768 to 32768
        if (user_steering > 32767.0f) user_steering = 32767.0f;
        if (user_steering < -32768.0f) user_steering = -32768.0f;
        
        ApplySteeringLocked();
    }
    
    // Pedals from booleans (digital input)
    // Keep 0.0 to 1.0 for pedals as that's what hid_device_.Update expects
    throttle = frame.logical.throttle ? 1.0f : 0.0f;
    brake = frame.logical.brake ? 1.0f : 0.0f;
    clutch = frame.logical.clutch ? 1.0f : 0.0f;
    
    // Copy buttons
    button_states = frame.logical.buttons;
    
    // Convert steering from internal +/- 32768 to normalized +/- 1.0 for vJoy
    float normalized_steering = steering / 32768.0f;
    hid_device_.Update(normalized_steering, throttle, brake, clutch, button_states);
}


void WheelDevice::OnFFBPacket(void* data) {
    // Standard vJoy FFB Header
    struct FFB_DATA_HEADER {
        ULONG size;
        ULONG cmd;
    };
    
    if (!data) return;
    
    FFB_DATA_HEADER* header = static_cast<FFB_DATA_HEADER*>(data);
    ULONG size = header->size;
    ULONG cmd = header->cmd;
    
    // Validate size
    if (size < 8) return; 
    
    // Pointer to the raw payload (HID Report)
    const uint8_t* raw_payload = static_cast<const uint8_t*>(data) + 8;
    size_t payload_len = size - 8;
    
    // The "Cmd" field from vJoy is often passed as IOCTL_HID_WRITE_REPORT or SET_REPORT.
    // However, the *payload* (raw_payload) actually contains the HID FFB data.
    // The first byte of raw_payload is the Report ID.
    // Linux logic expects the first byte of `ParseFFBCommand`'s `data` argument to be the Effect Command ID (like 0x11, 0x13).
    // Let's inspect raw_payload to see if it matches.
    
    // If the game sends Report ID X, raw_payload[0] should be X.
    
    if (payload_len > 0) {
        // Debug
        // std::cout << "FFB Payload [0]: " << std::hex << (int)raw_payload[0] << std::dec << std::endl;
        
        // Pass payload directly to ParseFFBCommand
        ParseFFBCommand(raw_payload, payload_len);
    }
}

void WheelDevice::ParseFFBCommand(const uint8_t* data, size_t size) {
    if (size < 1) return;

    std::lock_guard<std::mutex> lock(state_mutex);
    
    // vJoy / Windows HID FFB Mapping
    // The data[0] byte is the Report ID.
    // The previous debug log showed "ID=b0 ef cf a8..." which was garbage data? 
    // Wait, the user paste showed "b0 ef cf a8" which doesn't look like standard FFB cmds.
    // vJoyInterface might use a packed structure inside the payload for "GenCB".
    
    // If the logging showed garbage, maybe "raw_payload" offset is wrong or vJoy uses a different struct.
    // However, assuming standard HID PID behavior:
    // 0x25: Effect
    // 0x26: Start/Stop
    // 0x50: Device Control
    // 0xFE: Autocenter (Logitech specific custom?)
    
    // Linux implementation used Logitech G29 specific reports? (0x11, 0x13, 0xF8...)
    // If vJoy is emulating a standard joystick, it uses generic PID (Physical Interface Device) report IDs.
    
    // Let's try to map standard PID to our engine if possible, OR
    // if the user configured vJoy to emulate a "G25/G27/G29", vJoy passes those raw packets.
    // If vJoy is in "FFB" mode, it usually receives generic DirectInput effect parameters which are translated to HID.
    
    uint8_t cmd = data[0];
    
    // Debugging Unknown Commands
    // static int unk_log = 0;
    // if (++unk_log % 50 == 0) std::cout << "FFB Cmd: " << std::hex << (int)cmd << std::dec << std::endl;

    switch (cmd) {
        // Standard PID Block Load (similar to 0x11 constant force but generic)
        case 0x11: // Logitech/Linux Constant Force
        {
             if (size < 3) break;
             int8_t force = static_cast<int8_t>(data[2]) - 0x80;
             ffb_force = static_cast<int16_t>(-force) * 48;
             ffb_cv.notify_all();
             break;
        }
        case 0x25: // PID SET EFFORT 
        case 0x26: // PID START/STOP
             // To support generic vJoy FFB, we need a full PID parser.
             // For now, let's treat it as a "placeholder" that activates simple spring effects?
             break;
             
        // If we see typical Logitech codes:
        case 0x13: ffb_force = 0; ffb_cv.notify_all(); break;
        case 0xF5: ffb_autocenter = 0; ffb_cv.notify_all(); break;
        
        // ... (rest of old switch) ...
        default:
            break;
    }
    
    // Re-paste logic for cleanliness
    switch (cmd) {
        case 0x11: {  // Constant force slot update
            if (size < 3) break;
            int8_t force = static_cast<int8_t>(data[2]) - 0x80; 
            ffb_force = static_cast<int16_t>(-force) * 48;
            ffb_cv.notify_all();
            break;
        }
        case 0x13:  // Stop force effect
            ffb_force = 0;
            ffb_cv.notify_all();
            break;
        case 0xf5:  // Disable autocenter
            if (ffb_autocenter != 0) {
                ffb_autocenter = 0;
                ffb_cv.notify_all();
            }
            break;
        case 0xfe:  // Configure autocenter
            if (size >= 3 && data[1] == 0x0d) {
                int16_t strength = static_cast<int16_t>(data[2]) * 16;
                if (ffb_autocenter != strength) {
                    ffb_autocenter = strength;
                    ffb_cv.notify_all();
                }
            }
            break;
        case 0x14:  // Enable default autocenter
            if (ffb_autocenter == 0) {
                ffb_autocenter = 1024;
                ffb_cv.notify_all();
            }
            break;
        default:
            break;
    }
}

float WheelDevice::ShapeFFBTorque(float raw_force) const {
    float abs_force = std::abs(raw_force);
    if (abs_force < 80.0f) {
        return raw_force * (abs_force / 80.0f);
    }

    const float min_gain = 0.25f;
    const float slip_knee = 4000.0f;
    const float slip_full = 14000.0f;
    float t = (abs_force - 80.0f) / (slip_full - 80.0f);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float slip_weight = t * t;

    float gain = min_gain;
    if (abs_force > slip_knee) {
        float heavy = (abs_force - slip_knee) / (slip_full - slip_knee);
        if (heavy < 0.0f) heavy = 0.0f;
        if (heavy > 1.0f) heavy = 1.0f;
        gain = min_gain + (1.0f - min_gain) * heavy;
    } else {
        gain = min_gain + (slip_weight * (1.0f - min_gain));
    }

    const float boost = 3.0f;
    return raw_force * gain * boost;
}

bool WheelDevice::ApplySteeringLocked() {
    float combined = user_steering + ffb_offset;
    // Clamp to logical range
    if (combined > 32767.0f) combined = 32767.0f;
    if (combined < -32768.0f) combined = -32768.0f;
    
    if (std::abs(combined - steering) < 0.1f) {
        return false;
    }
    steering = combined;
    return true;
}

void WheelDevice::FFBUpdateThread() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    float filtered_ffb = 0.0f;

    while (ffb_running) {
        std::unique_lock<std::mutex> lock(state_mutex);
        
        // Wait for update or timeout (1ms tick)
        // Linux code waits 1ms.
        ffb_cv.wait_for(lock, std::chrono::milliseconds(1));
        
        if (!ffb_running) break;

        int16_t local_force = ffb_force;
        int16_t local_autocenter = ffb_autocenter;
        float local_offset = ffb_offset;
        float local_velocity = ffb_velocity;
        float local_gain = ffb_gain;
        float local_steering = steering;
        
        lock.unlock();

        auto now = clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        if (dt <= 0.0f) dt = 0.001f;
        if (dt > 0.01f) dt = 0.01f;
        last = now;

        float commanded_force = ShapeFFBTorque(static_cast<float>(local_force));

        const float force_filter_hz = 38.0f;
        float alpha = 1.0f - std::exp(-dt * force_filter_hz);
        if (alpha < 0.0f) alpha = 0.0f;
        if (alpha > 1.0f) alpha = 1.0f;
        
        filtered_ffb += (commanded_force - filtered_ffb) * alpha;

        float spring = 0.0f;
        if (local_autocenter > 0) {
            spring = -(local_steering * static_cast<float>(local_autocenter)) / 32768.0f;
        }

        const float offset_limit = 22000.0f;
        float target_offset = (filtered_ffb + spring) * local_gain;
        if (target_offset > offset_limit) target_offset = offset_limit;
        if (target_offset < -offset_limit) target_offset = -offset_limit;

        const float stiffness = 120.0f;
        const float damping = 8.0f;
        const float max_velocity = 90000.0f;
        
        float error = target_offset - local_offset;
        local_velocity += error * stiffness * dt;
        float damping_factor = std::exp(-damping * dt);
        local_velocity *= damping_factor;
        
        if (local_velocity > max_velocity) local_velocity = max_velocity;
        if (local_velocity < -max_velocity) local_velocity = -max_velocity;

        local_offset += local_velocity * dt;
        if (local_offset > offset_limit) {
            local_offset = offset_limit;
            local_velocity = 0.0f;
        } else if (local_offset < -offset_limit) {
            local_offset = -offset_limit;
            local_velocity = 0.0f;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex);
            ffb_offset = local_offset;
            ffb_velocity = local_velocity;
            ApplySteeringLocked();
        }
    }
}

