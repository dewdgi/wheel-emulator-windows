# Wheel HID Emulator - Architecture Documentation

**Last Updated:** November 18, 2025  
**Version:** 2.0 (Complete FFB Implementation)

---

## Overview

This project transforms a standard keyboard and mouse into a Logitech G29 Racing Wheel with full Force Feedback support. It creates a virtual USB HID device that is indistinguishable from a real G29 wheel to games and racing simulators.

**Key Features:**
- Real USB device emulation (USB Gadget ConfigFS)
- Full Force Feedback (FFB) support with bidirectional communication
- Physics-based steering simulation
- 25 buttons + D-Pad + 4 axes (steering, brake, throttle, unused Y)
- Configurable sensitivity and device paths
- Interactive device detection mode

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                         Main Loop                            │
│                        (125 Hz)                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ 1. Read Keyboard/Mouse Input (evdev)                   │ │
│  │ 2. Check Ctrl+M Toggle (Enable/Disable)                │ │
│  │ 3. Update Gamepad State (buttons, pedals, dpad)        │ │
│  │ 4. Update Mouse Input → user_torque                    │ │
│  │ 5. Send HID Report (steering, pedals, buttons)         │ │
│  │ 6. Process UHID Events (FFB commands from game)        │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────────┐
│                      FFB Physics Thread                      │
│                        (125 Hz)                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ 1. Calculate total_torque:                             │ │
│  │    = ffb_force (from game)                             │ │
│  │    + user_torque (from mouse)                          │ │
│  │    + autocenter_spring (pull to center)                │ │
│  │ 2. Update velocity: velocity += torque * 0.001         │ │
│  │ 3. Apply damping: velocity *= 0.98                     │ │
│  │ 4. Update position: steering += velocity               │ │
│  │ 5. Clamp steering to [-32768, 32767]                   │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────────┐
│                  USB Gadget Polling Thread                   │
│                        (Event-Driven)                        │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ - Waits for host polls using poll(POLLIN|POLLOUT)     │ │
│  │ - Sends INPUT reports (wheel state) on POLLOUT         │ │
│  │ - Receives OUTPUT reports (FFB commands) on POLLIN     │ │
│  │ - Bidirectional USB HID communication                  │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## Component Details

### 1. Input System (`input.cpp`)

**Purpose:** Read raw keyboard and mouse events from `/dev/input/eventX` devices.

**Device Discovery:**
- Priority-based auto-detection of keyboards and mice
- Filters out unwanted devices (consumer control, touchpads, etc.)
- Supports explicit device paths from config
- Interactive detection mode (`--detect`) for user identification

**Key State Tracking:**
- Maintains boolean array `keys[KEY_MAX]` for all key states
- Accumulates mouse X delta per frame (steering input)
- Edge detection for Ctrl+M toggle (enable/disable emulation)

**Device Grabbing:**
- `EVIOCGRAB` for exclusive device access when emulation enabled
- Prevents desktop from receiving input during gameplay
- Released on Ctrl+M toggle or Ctrl+C exit

---

### 2. Virtual Device Creation (`gamepad.cpp`)

**Three Methods (Priority Order):**

#### Method 1: USB Gadget ConfigFS (Preferred)
- **Path:** `/dev/hidg0`
- **Creates:** Real USB HID device via Linux kernel's USB Gadget framework
- **Driver Binding:** Kernel's `hid-lg` driver binds automatically (VID:046d PID:c24f)
- **FFB Support:** Full bidirectional communication via OUTPUT reports
- **Requirements:** `CONFIG_USB_CONFIGFS=y`, `dummy_hcd` or real UDC
- **Advantages:** 
  - Games see authentic USB device on bus
  - Proper USB interface enumeration
  - Windows/Wine compatibility
  - Native driver support

#### Method 2: UHID (Fallback)
- **Path:** `/dev/uhid`
- **Creates:** Userspace HID device via UHID kernel interface
- **Driver Binding:** Creates hidraw device, but not proper USB device
- **FFB Support:** OUTPUT reports via `UHID_OUTPUT` events
- **Requirements:** `uhid` kernel module
- **Advantages:**
  - Provides hidraw interface (better than uinput)
  - FFB communication possible
  - No USB Gadget hardware needed

#### Method 3: UInput (Legacy)
- **Path:** `/dev/uinput`
- **Creates:** Input device via uinput subsystem
- **Driver Binding:** Generic joystick/gamepad, no hid-lg
- **FFB Support:** Registers capabilities but no actual FFB
- **Advantages:**
  - Always available
  - Simple API
- **Disadvantages:**
  - No HIDRAW device
  - No FFB communication
  - Games may not recognize as G29

---

### 3. HID Descriptor

**Logitech G29 HID Report Descriptor:**
```
Application: Joystick (0x04)
- INPUT Report (13 bytes, no report ID):
  - ABS_X (16-bit): Steering wheel (0-65535, center=32768)
  - ABS_Y (16-bit): Unused, constant 65535
  - ABS_Z (16-bit): Brake pedal (inverted: 65535=rest, 0=pressed)
  - ABS_RZ (16-bit): Throttle pedal (inverted: 65535=rest, 0=pressed)
  - HAT0 (4-bit): D-Pad (8 directions + neutral)
  - Buttons (25-bit): BTN_1 through BTN_25
  
- OUTPUT Report (7 bytes, no report ID):
  - FFB command buffer for hid-lg driver
  - Commands: constant force, autocenter, range, LEDs, etc.
```

**Critical Design:**
- **No Report IDs:** G29 uses simple descriptor without report IDs
- **Inverted Pedals:** Real G29 firmware uses inverted axes (32767=rest)
- **OUTPUT Report Required:** Must be present for hid-lg driver binding

---

### 4. Force Feedback System

**Architecture:** Physics-based simulation running in dedicated thread.

#### FFB Command Processing

**Game → Driver → Emulator:**
1. Game sends FFB effect via kernel FFB API
2. Kernel's `hid-lg` driver translates to G29 protocol
3. Driver sends 7-byte OUTPUT report to device
4. Emulator parses command in `ParseFFBCommand()`

**Supported Commands:**
- `0x11`: Constant force effect (main steering forces)
- `0x13`: Stop effect / disable force
- `0x14`: Enable autocenter spring
- `0xf5`: Disable autocenter spring
- `0xfe`: Set autocenter parameters (strength, spring rate)
- `0xf8`: Extended commands (wheel range, LEDs, mode switching)

#### Physics Model

**State Variables:**
- `steering` (float): Current wheel position [-32768, 32767]
- `velocity` (float): Current rotation speed (units per frame)
- `ffb_force` (int16_t): Force from game [-32768, 32767]
- `user_torque` (float): Force from mouse input
- `ffb_autocenter` (int16_t): Spring force pulling to center

**Physics Loop (125 Hz):**
```cpp
// 1. Calculate total torque
total_torque = ffb_force + user_torque + autocenter_spring

// 2. Torque → Velocity (F=ma)
velocity += total_torque * 0.001f

// 3. Apply damping (friction)
velocity *= 0.98f

// 4. Velocity → Position
steering += velocity

// 5. Clamp to limits
steering = clamp(steering, -32768, 32767)
```

**Key Properties:**
- **Linear:** All forces add linearly, no exponential effects
- **Damping:** 2% velocity loss per frame (98% retention)
- **Responsive:** User can override FFB forces with mouse input
- **Realistic:** Autocenter provides gentle pull to center
- **Race-Free:** Mutex-protected state access

---

### 5. Input Mapping

#### Steering (Mouse X Axis)
```cpp
// Mouse delta → User torque
if (abs(delta) >= 2) {  // 2-pixel deadzone for jitter
    user_torque = delta * sensitivity * 20.0f
}
```

**Characteristics:**
- **Linear scaling:** Proportional to sensitivity setting
- **Deadzone:** ±2 pixels filters sensor jitter
- **Force-based:** Adds to physics simulation, not direct position
- **Balanced:** Scaled to match FFB force magnitude (20x vs old 200x)

**Example:**
- Sensitivity = 50, Mouse = 10 pixels
- `user_torque = 10 * 50 * 20 = 10,000`
- Comparable to max FFB force (32,767)

#### Pedals (W/S Keys)
```cpp
// Ramping (3% per frame at 125Hz)
throttle += (W_pressed ? 3.0f : -3.0f)
brake += (S_pressed ? 3.0f : -3.0f)
throttle = clamp(throttle, 0.0f, 100.0f)
brake = clamp(brake, 0.0f, 100.0f)

// Convert to inverted G29 range
throttle_axis = 65535 - (throttle * 655.35f)
brake_axis = 65535 - (brake * 655.35f)
```

**Characteristics:**
- **Hold-to-accelerate:** Pressing W gradually increases throttle
- **Auto-release:** Releasing key gradually decreases
- **Independent:** Can press both simultaneously
- **Inverted output:** Matches real G29 hardware

#### Buttons (Keyboard Keys)
25 buttons mapped to keys Q, E, F, G, H, R, T, Y, U, I, O, P, 1-9, 0, LShift, Space, Tab

#### D-Pad (Arrow Keys)
8-directional HAT switch: Up, Down, Left, Right, and diagonals

---

### 6. Main Loop Flow

**Initialization:**
1. Check root privileges
2. Load config from `/etc/wheel-emulator.conf`
3. Create virtual G29 device (USB Gadget → UHID → uinput)
4. Discover keyboard and mouse (config paths or auto-detect)
5. Start FFB physics thread
6. Start USB Gadget polling thread (if applicable)
7. Begin in **disabled** state (devices not grabbed)

**Main Loop (125 Hz / 8ms):**
```
while (running) {
    // 1. Read input events
    input.Read(mouse_dx)
    
    // 2. Check Ctrl+M toggle
    if (input.CheckToggle()) {
        enabled = !enabled
        input.Grab(enabled)  // Exclusive access
    }
    
    // 3. Update state (if enabled)
    if (enabled) {
        gamepad.UpdateSteering(mouse_dx, sensitivity)
        gamepad.UpdateThrottle(W_pressed)
        gamepad.UpdateBrake(S_pressed)
        gamepad.UpdateButtons(input)
        gamepad.UpdateDPad(input)
        gamepad.SendState()  // Send HID report
    }
    
    // 4. Process FFB events
    gamepad.ProcessUHIDEvents()
    
    // 5. Sleep to maintain 125Hz
    usleep(8000)  // 8ms
}
```

**Cleanup (Ctrl+C):**
1. Stop FFB thread
2. Stop USB Gadget thread
3. Ungrab input devices
4. Destroy virtual device
5. Close file descriptors

---

## Threading Model

### Thread 1: Main Loop (125 Hz)
- Reads keyboard/mouse input
- Updates button/pedal state
- Sends HID reports
- Processes incoming FFB commands
- **Mutex:** Locks `state_mutex` when updating `user_torque`

### Thread 2: FFB Physics (125 Hz)
- Runs physics simulation
- Calculates steering position from forces
- **Mutex:** Locks `state_mutex` when updating `steering`, `velocity`

### Thread 3: USB Gadget Polling (Event-Driven)
- Only active in USB Gadget mode
- Waits for host polls using `poll()`
- Sends INPUT reports when host requests
- Receives OUTPUT reports (FFB commands) from host
- **Mutex:** Locks `state_mutex` when reading state for HID report

**Synchronization:**
- All shared state protected by `state_mutex`
- No deadlocks (short critical sections)
- Consistent 125Hz timing across threads

---

## Critical Bug Found: Race Condition in SendState()

### The Problem

In `SendState()` and `BuildHIDReport()`, the code reads `steering` without locking the mutex:

```cpp
void GamepadDevice::SendState() {
    // NO MUTEX LOCK HERE!
    EmitEvent(EV_ABS, ABS_X, static_cast<int16_t>(steering));  // ← RACE!
}

std::vector<uint8_t> GamepadDevice::BuildHIDReport() {
    // NO MUTEX LOCK HERE!
    uint16_t steering_u = static_cast<uint16_t>(static_cast<int16_t>(steering) + 32768);  // ← RACE!
}
```

Meanwhile, FFB thread modifies `steering`:
```cpp
void GamepadDevice::FFBUpdateThread() {
    std::lock_guard<std::mutex> lock(state_mutex);
    steering += velocity;  // ← Writes to steering
}
```

### Why This Breaks Everything

**Race condition scenario:**
1. Main thread calls `SendState()` → reads `steering` (no lock)
2. FFB thread interrupts → locks mutex → modifies `steering` → unlocks
3. Main thread continues → reads `throttle`, `brake` (old values)
4. **Result:** HID report has inconsistent state (new steering, old pedals)

**This causes:**
- Wheel position jumps/jitters in game
- Game may reject malformed reports
- Device appears non-responsive or broken
- **Wheel not detected at all** if reports are too corrupted

### The Fix

Add mutex locking to both code paths:

**Path 1: UHID/USB Gadget (primary)**
```cpp
void GamepadDevice::SendState() {
    if (use_uhid) {
        SendUHIDReport();  // Calls BuildHIDReport()
        return;
    }
    // ... uinput code
}

void GamepadDevice::SendUHIDReport() {
    std::vector<uint8_t> report_data = BuildHIDReport();  // ← Lock happens here
    // Send report...
}

std::vector<uint8_t> GamepadDevice::BuildHIDReport() {
    std::lock_guard<std::mutex> lock(state_mutex);  // ← LOCKED for UHID path
    // Read all state safely...
    return report;
}
```

**Path 2: UInput (fallback)**
```cpp
void GamepadDevice::SendState() {
    if (use_uhid) {
        // ... early return
    }
    
    std::lock_guard<std::mutex> lock(state_mutex);  // ← LOCKED for uinput path
    
    // Read steering, throttle, brake, buttons safely
    EmitEvent(EV_ABS, ABS_X, static_cast<int16_t>(steering));
    // ... rest of code
}
```

**Result:** Both code paths now have mutex protection when reading shared state.

---

## Configuration File

**Location:** `/etc/wheel-emulator.conf`

**Format:**
```ini
[devices]
keyboard=/dev/input/event6
mouse=/dev/input/event11

[sensitivity]
sensitivity=50
```

**Parameters:**
- `keyboard`: Explicit keyboard device path (optional, auto-detects if empty)
- `mouse`: Explicit mouse device path (optional, auto-detects if empty)
- `sensitivity`: Steering sensitivity 1-100 (default: 50)
  - Higher = more responsive steering
  - Multiplier: `sensitivity * 20.0f` for user torque

---

## Device Detection Mode

**Usage:** `sudo ./wheel-emulator --detect`

**Process:**
1. Opens all `/dev/input/event*` devices
2. Filters by capabilities (EV_KEY for keyboard, EV_REL for mouse)
3. **Keyboard detection (5 seconds):**
   - User types on keyboard
   - Counts key press events per device
   - Selects device with most events
4. **Mouse detection (5 seconds):**
   - User moves mouse
   - Counts REL_X movement events per device
   - Selects device with most events
5. Automatically updates `/etc/wheel-emulator.conf`
6. Ready to run normally

---

## Technical Specifications

### Device Identity
- **Vendor ID:** 0x046d (Logitech, Inc.)
- **Product ID:** 0xc24f (G29 Racing Wheel)
- **Version:** 0x0111 (273 decimal)
- **Bus Type:** BUS_USB
- **Device Name:** "Logitech G29 Driving Force Racing Wheel"

### Axes (4 total)
- **ABS_X:** Steering wheel [-32768 to 32767, center=0]
- **ABS_Y:** Unused [constant 32767]
- **ABS_Z:** Brake pedal [32767=rest, -32768=pressed] **INVERTED**
- **ABS_RZ:** Throttle pedal [32767=rest, -32768=pressed] **INVERTED**

### Buttons (25 total)
- BTN_TRIGGER through BTN_BASE6 (13 buttons)
- BTN_DEAD (1 button)
- BTN_TRIGGER_HAPPY1 through BTN_TRIGGER_HAPPY12 (12 buttons)

### D-Pad (HAT Switch)
- ABS_HAT0X: Horizontal [-1, 0, 1]
- ABS_HAT0Y: Vertical [-1, 0, 1]
- Combined: 8 directions + neutral

### Update Rate
- **Main Loop:** 125 Hz (8ms period)
- **FFB Physics:** 125 Hz (8ms period)
- **USB Polling:** Event-driven (host-initiated)

---

## Physics Parameters

### Torque Integration
- **Coefficient:** 0.001
- **Effect:** Converts torque to velocity change
- **Tuning:** Higher = more responsive, lower = more inertia

### Damping Factor
- **Value:** 0.98 (2% loss per frame)
- **Effect:** Simulates friction, prevents runaway
- **Tuning:** Higher (0.99) = less friction, lower (0.95) = more friction

### Mouse Scaling
- **Formula:** `delta * sensitivity * 20.0f`
- **Previous:** Was 200.0f (10x too strong)
- **Fix:** Reduced to 20.0f to balance with FFB forces

### Deadzone
- **Value:** ±2 pixels
- **Purpose:** Filter mouse sensor jitter
- **Effect:** Tiny movements don't fight FFB

---

## Build & Run

### Build
```bash
make clean
make
```

### First Run (Device Detection)
```bash
sudo ./wheel-emulator --detect
# Follow prompts: type on keyboard, move mouse
# Config auto-updated
```

### Normal Run
```bash
sudo ./wheel-emulator
# Press Ctrl+M to enable
# Press Ctrl+M to disable
# Press Ctrl+C to exit
```

### Cleanup USB Gadget
```bash
./cleanup_gadget.sh
# Removes USB Gadget if stuck
```

---

**Document Version:** 2.0  
**Code Version:** Current (FFB physics improvements + race condition fix needed)  
**Author:** dewdgi  
**Repository:** https://github.com/dewdgi/wheel-hid-emulator
