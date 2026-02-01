#include "wheel_device.h"
#include "logging/logger.h"
#include <iostream>
#include <algorithm>
#include <windows.h>
#include "public.h"
#include "vjoyinterface.h"

namespace {
constexpr const char* kTag = "wheel_device";
}

static void CALLBACK FFB_Callback(PVOID data, PVOID user_data) {
    if (user_data) {
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

void WheelDevice::SendUpdateLocked() {
    float normalized_steering = steering / 32768.0f;
    hid_device_.Update(normalized_steering, throttle, brake, clutch, button_states);
}

void WheelDevice::ProcessInputFrame(const InputFrame& frame, int sensitivity) {
    std::lock_guard<std::mutex> lock(state_mutex);
    
    // Linux Accumulation Logic Match
    constexpr float base_gain = 0.05f; 
    float steering_delta = static_cast<float>(frame.mouse_dx) * static_cast<float>(sensitivity) * base_gain;
    
    // Clamp delta speed
    const float max_step = 2000.0f;
    if (steering_delta > max_step) steering_delta = max_step;
    if (steering_delta < -max_step) steering_delta = -max_step;
    
    if (steering_delta != 0.0f) {
        user_steering += steering_delta;
        
        // Clamp Logical Range (-32768..32767)
        if (user_steering > 32767.0f) user_steering = 32767.0f;
        if (user_steering < -32768.0f) user_steering = -32768.0f;
        
        ApplySteeringLocked();
    }
    
    // Pedals
    throttle = frame.logical.throttle ? 1.0f : 0.0f;
    brake = frame.logical.brake ? 1.0f : 0.0f;
    clutch = frame.logical.clutch ? 1.0f : 0.0f;
    
    // Copy buttons
    button_states = frame.logical.buttons;
    
    SendUpdateLocked();
}

void WheelDevice::OnFFBPacket(void* data) {
    if (!data) return;

    // Use vJoy SDK helper functions to process the packet strictly
    FFB_DATA* packet = static_cast<FFB_DATA*>(data);
    FFBPType type = PT_CONSTREP; // Default init
    
    if (Ffb_h_Type(packet, &type) != ERROR_SUCCESS) {
        return; 
    }

    std::lock_guard<std::mutex> lock(state_mutex);

    switch (type) {
        case PT_CONSTREP: // Usage Set Constant Force Report
        {
            FFB_EFF_CONSTANT effect;
            if (Ffb_h_Eff_Constant(packet, &effect) == ERROR_SUCCESS) {
                // Magnitude: -10000 to 10000
                // We clamp just in case vJoy sends out of bounds
                long raw_mag = effect.Magnitude;
                if (raw_mag > 10000) raw_mag = 10000;
                if (raw_mag < -10000) raw_mag = -10000;

                // Map vJoy +/- 10000 to Linux Physics +/- 6144
                // Linux: 0xFF -> -127 force -> -6096 (Left)
                // Linux: 0x00 -> +128 force -> +6144 (Right)
                // vJoy: +10000 (Right) -> +6144
                // vJoy: -10000 (Left)  -> -6144
                // We use POSITIVE mapping (Raw Logic). 
                ffb_force = static_cast<int16_t>(static_cast<float>(raw_mag) * 0.6144f);
                ffb_cv.notify_all();
            }
            break;
        }

        case PT_EFOPREP: // Usage Effect Operation Report
        {
            FFB_EFF_OP op;
            if (Ffb_h_EffOp(packet, &op) == ERROR_SUCCESS) {
                if (op.EffectOp == EFF_STOP) {
                    ffb_force = 0;
                }
                ffb_cv.notify_all();
            }
            break;
        }
        
        case PT_CTRLREP: // Usage PID Device Control
        {
            FFB_CTRL control;
            if (Ffb_h_DevCtrl(packet, &control) == ERROR_SUCCESS) {
                 switch (control) {
                     case CTRL_STOPALL:
                     case CTRL_DEVRST:
                        ffb_force = 0;
                        break;
                     default:
                        break;
                 }
                 ffb_cv.notify_all();
            }
            break;
        }

        default:
            break;
    }
}

void WheelDevice::ParseFFBCommand(const uint8_t* data, size_t size) {
    // Unused
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
        
        static int phys_log = 0;
        if ((std::abs(target_offset) > 10.0f || std::abs(local_offset) > 10.0f) && ++phys_log % 500 == 0) {
            std::cout << "FFB Phys: Steer=" << local_steering 
                      << " Spring=" << spring 
                      << " Offset=" << local_offset 
                      << " Tgt=" << target_offset << std::endl;
        }

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
            SendUpdateLocked();
        }
    }
}
