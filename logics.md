## Wheel HID Emulator Logic (November 2025)

This document matches the current gadget-only implementation. Every subsystem described below exists in the repository today, and any outdated UHID/uinput paths referenced in older notes have been removed from both code and documentation.

---

## Runtime Stages

1. **Startup**
   - `main.cpp` ensures root, installs SIGINT handler, loads `/etc/wheel-emulator.conf`.
   - `GamepadDevice::Create()` provisions the ConfigFS gadget, opens `/dev/hidg0`, and starts the three helper threads (FFB physics, gadget writer, gadget OUTPUT).
   - `Input` scans `/dev/input/event*`, honoring optional overrides from the config.
2. **Enabled Loop**
   - Main loop waits up to 8 ms for input, drains every keyboard/mouse fd, and checks Ctrl+M.
   - When enabled, the loop updates steering, pedals, buttons, and D-pad, then calls `SendState()` to wake the gadget writer thread.
   - The gadget writer serializes a 13-byte HID report that matches the Logitech G29 descriptor and writes it to `/dev/hidg0` until the host accepts it.
   - The gadget OUTPUT thread parses 7-byte OUTPUT reports for FFB, waking the physics loop as soon as torque data arrives.
3. **Shutdown**
   - Ctrl+C flips the global `running` flag, unblocks every condition variable, and joins all threads.
   - The gadget is left intact (neutral frame already sent) so a subsequent `./wheel-emulator` run simply reuses `g29wheel` and its previously bound UDC without re-creating descriptors. Manually remove it with `echo '' | sudo tee /sys/kernel/config/usb_gadget/g29wheel/UDC` followed by deleting the gadget directory if you truly need a clean slate.

---

## Modules & Responsibilities

### `src/main.cpp`
- Owns the lifecycle: config load, gadget creation, hotplug discovery, and the control loop.
- Maintains `running` and the `Ctrl+M` toggle state (via `Input::CheckToggle`).
- Computes frame delta for steering smoothing, clamps `dt` to 50 ms to prevent physics spikes, and enforces graceful shutdown.

### `src/input.cpp`
- Tracks every relevant `/dev/input/event*` device with capabilities, per-device key shadows, and grab state.
- Auto-detects keyboard/mouse devices unless both overrides are specified, in which case only the pinned fds stay open.
- Implements `WaitForEvents(timeout_ms)` via `poll`, so the main loop sleeps until activity or timeout.
- Aggregates key presses into `keys[KEY_MAX]`, accumulates mouse X delta, and exposes `IsKeyPressed(keycode)` lookups.
- `Grab(bool)` now returns success/failure, validates that both keyboard and mouse are actually grabbed, and immediately releases everything if either class is missing. Discovery still runs while ungrabbed so new devices can be pulled in before the next toggle.
- `ResyncKeyStates()` only does real work when `resync_pending` is true (i.e., a new keyboard was discovered). `GamepadDevice::SetEnabled(true)` still calls it so pending resyncs happen immediately, but steady-state toggles skip the expensive EVIOCGKEY sweep entirely.

### `src/gamepad.cpp`
- Holds the canonical wheel state behind `state_mutex`: steering, user steering, FFB offset/velocity, three pedals, D-pad axes, 26 button bits, enable flag, and FFB parameters.
- `CreateUSBGadget()` performs the full ConfigFS ritual (IDs, strings, hid.usb0 function, configs/c.1 link, and UDC bind) and opens `/dev/hidg0` non-blocking.
- `BindUDC()` / `UnbindUDC()` toggle the USB connection instantly by writing to the gadget’s `UDC` file. The emulator boots with the gadget detached and only binds the controller once all input devices are grabbed and the neutral/snapshot frames are staged; disabling (or losing the mouse) unbinds immediately so the host sees a complete disconnect.
- `USBGadgetPollingThread()` is the only writer to `/dev/hidg0`. It waits on `state_cv`, builds a 13-byte report with `BuildHIDReport()`, then calls `WriteHIDBlocking()` until the transfer completes. A short "warmup" burst (25 frames) is pushed any time we re-enable so games that are sitting in input menus still see fresh data even if nothing is moving yet.
- `USBGadgetOutputThread()` polls for OUTPUT data, reassembles 7-byte packets, and hands them to `ParseFFBCommand()` for decoding.
- `FFBUpdateThread()` runs at ~125 Hz: shapes torque, blends autocenter springs, applies gain, feeds the damped spring model, and nudges steering by applying `ffb_offset` before clamping to ±32768.
- `ToggleEnabled()` flips the `enabled` flag under the mutex, grabs/ungrabs input, calls `ResyncKeyStates()` (which is a no-op unless a new device arrived), reapplies whatever pedals/buttons are currently held via a single snapshot write, schedules the warmup burst, and logs the new mode so the host always sees a current frame when control changes.
- `ProcessInputFrame()` consumes mouse delta plus the current keyboard snapshot in one lock-protected block, and it bails out entirely while `output_enabled` is false so we never mutate state mid-handshake.
- `SendNeutral(reset_ffb)` injects a neutral frame and, unless we explicitly ask for a reset, preserves the force-feedback state so quickly toggling Ctrl+M no longer clears road feel.
- Enable handshake: `SetEnabled(true)` grabs devices, captures a snapshot, forces `output_enabled=false`, applies a neutral state, writes that HID frame directly, applies the snapshot, writes again, then finally enables asynchronous output and schedules the warmup burst. The host only ever sees whole frames in the right order.
- Warmup burst: once `output_enabled` flips true, `GamepadDevice` emits a few consecutive frames so ACC never sees half-pressed pedals or missing axes after a toggle.
- `output_enabled` gates both HID IN and OUT: nothing is transmitted or parsed while disabled, and FFB physics sleeps until the toggle completes, preventing random packets from confusing the host.

### `src/config.cpp`
- Reads `/etc/wheel-emulator.conf`. If missing, writes a documented default that matches the code (including the KEY_ENTER button 26 binding and clutch axis description).
- Recognized keys: `[devices] keyboard/mouse`, `[sensitivity] sensitivity` (1–100), `[ffb] gain` (0.1–4.0). Optional `[button_mapping]` entries are informational only.

---

## USB Gadget Flow

1. `CreateUSBGadget()` loads `libcomposite`/`dummy_hcd` (best-effort) and ensures ConfigFS is mounted.
2. Any previous gadget with the same name is removed to avoid collisions.
3. The Logitech G29 descriptor (26 buttons, hat, four 16-bit axes, 7-byte OUTPUT report) is written to `functions/hid.usb0/report_desc` and `report_length` is set to 13.
4. The hid function is linked into `configs/c.1`, string tables are populated, and the first available UDC is bound.
5. `/dev/hidg0` opens in non-blocking read/write mode. All IN traffic goes through `WriteHIDBlocking`; all OUT traffic is drained in `ReadGadgetOutput` to keep the kernel queue empty.

Because UHID/uinput is gone, failure to create the gadget is fatal and surfaces clear instructions about ConfigFS/UDC requirements.

---

## Persistent Gadget Lifecycle

- `GamepadDevice::CreateUSBGadget()` always searches for the existing `g29wheel` tree before creating anything. If the descriptor, hid function, and config are already present from a previous run, it simply reuses them and just reopens `/dev/hidg0`.
- The gadget stays on disk even after you kill the emulator. When the process exits it releases file descriptors and threads but intentionally leaves ConfigFS entries in place so the very next launch binds instantly and the host keeps seeing the exact same VID/PID/serial combination.
- Enable/disable toggles (`Ctrl+M`) never destroy the gadget. Disabling merely unbinds the UDC, sends a neutral frame, and releases input grabs so the host experiences a clean unplug; re-enabling binds the same gadget again a few milliseconds later.
- If you truly need to remove the gadget (for example before loading a different USB gadget stack), run:

   ```bash
   echo '' | sudo tee /sys/kernel/config/usb_gadget/g29wheel/UDC
   sudo rm -rf /sys/kernel/config/usb_gadget/g29wheel
   ```

   ## Wheel HID Emulator Logic (November 2025)

   This document reflects the current USB-gadget-only implementation. UHID/uinput paths are gone, so everything below assumes a ConfigFS-capable kernel exposing `/dev/hidg0`.

   ---

   ## Runtime Flow

   ### 1. Startup
   - `main.cpp` refuses to run unless the process is root, installs the SIGINT handler, and loads `/etc/wheel-emulator.conf`.
   - `GamepadDevice::Create()` creates or reuses the `g29wheel` gadget tree, binds the first available UDC immediately, opens `/dev/hidg0`, primes a neutral HID frame, and launches the three helper threads (`USBGadgetPollingThread`, `USBGadgetOutputThread`, `FFBUpdateThread`).
   - `Input` discovers keyboard/mouse devices (with optional overrides) and marks itself ready for toggling.

   ### 2. Main Loop
   - Sleeps up to 8 ms via `Input::WaitForEvents`, then drains every device with `Input::Read`, which aggregates key state and mouse deltas.
   - Detects Ctrl+M with `Input::CheckToggle`; loss of a grabbed keyboard/mouse triggers a forced `SetEnabled(false)` so the host sees neutral data.
   - When enabled, `GamepadDevice::ProcessInputFrame` captures a snapshot, applies steering deltas scaled by `sensitivity`, and sets `state_dirty` so the gadget writer pushes a new 13-byte report.
   - The loop still tracks a `dt` value (clamped 0–50 ms) for future smoothing work, but the current build does not feed it anywhere else.

   ### 3. Shutdown
   - SIGINT flips `running=false`. The main loop disables the emulator, wakes every condition variable, joins helper threads, and releases any grabs.
   - The ConfigFS gadget is left in place and remains bound, already holding a neutral HID frame. A subsequent launch simply reopens `/dev/hidg0` and resumes streaming.
   - Manual cleanup (only if you need to unload the gadget) requires echoing an empty string into `/sys/kernel/config/usb_gadget/g29wheel/UDC` and deleting the directory, mirroring `DestroyUSBGadget()`.

   ---

   ## Major Modules

   ### `src/main.cpp`
   - Wires together config loading, gadget creation, input discovery, and the Ctrl+M toggle.
   - Calls `Input::WaitForEvents`, `Input::Read`, `Input::CheckToggle`, and `GamepadDevice::ProcessInputFrame` on every loop iteration.
   - On exit, forces `SetEnabled(false)`, wakes helper threads (`notify_all_shutdown`), runs `ShutdownThreads`, and releases all grabs.

   ### `src/input.cpp`
   - Keeps a list of `/dev/input/event*` handles with capability flags, grab state, and per-device `key_shadow` buffers so multiple keyboards can drive the same logical key.
   - `DiscoverKeyboard`/`DiscoverMouse` optionally pin devices; otherwise `RefreshDevices` auto-scans when idle and hotplugs new hardware mid-run.
   - `Read` drains each fd (up to 256 events per pass), coalesces REL_X into `mouse_dx`, maintains reference-counted `keys[KEY_MAX]`, and timestamps activity to avoid scanning while the user is typing.
   - `Grab(true)` issues `EVIOCGRAB` on every capable device, aborts if either class cannot be grabbed, and releases everything on failure so the host never keeps a half-grabbed input.
   - `ResyncKeyStates` uses `EVIOCGKEY` only when `resync_pending` is set (triggered by new keyboards or overrides) so steady toggles stay cheap.
   - `CheckToggle` fires once per Ctrl+M press even if both keys stay held, thanks to `prev_toggle` edge tracking.
   - `AllRequiredGrabbed` ensures both keyboard and mouse are locked before streaming; losing either forces a disable in `main`.

   ### `src/gamepad.cpp`
   - Owns the canonical wheel state (`state_mutex` protects steering, three pedals, hat, 26 buttons, FFB offset/velocity, and the enabled flag).
   - `CreateUSBGadget` loads `libcomposite`/`dummy_hcd` (best-effort), mounts ConfigFS if needed, builds the Logitech descriptor, sets strings/IDs, links `hid.usb0` into `configs/c.1`, binds the UDC immediately, and opens `/dev/hidg0`.
   - Enabling: `SetEnabled(true)` grabs input, resyncs keys if necessary, captures a snapshot, temporarily forces `output_enabled=false`, applies a neutral state (without resetting FFB), builds neutral + snapshot reports, waits for the gadget endpoint to become writable, writes both reports synchronously, ensures gadget threads are running, then flips `output_enabled=true` and schedules 25 warmup frames.
   - Disabling: `SetEnabled(false)` stops warmup/output, applies a neutral state with FFB reset, writes that report via `WriteReportBlocking`, and releases the grab. The gadget stays bound, so the OS keeps seeing a neutral Logitech wheel instead of a USB disconnect.
   - `ProcessInputFrame` short-circuits unless both `enabled` and `output_enabled` are true, ensuring no state mutation happens mid-handshake.
   - `USBGadgetPollingThread` is the sole HID IN writer. It watches `state_dirty` and `warmup_frames`, serializes a 13-byte report (`BuildHIDReportLocked`), and uses `WriteHIDBlocking` with back-off/reopen logic for transient EPIPE/ESHUTDOWN errors.
   - `USBGadgetOutputThread` drains `/dev/hidg0` into 7-byte packets, honoring `output_enabled` before forwarding them to `ParseFFBCommand`.
   - `FFBUpdateThread` runs at ~125 Hz, filters force, blends autocenter springs, applies the critically damped model (stiffness 120, damping 8, ±22 k offset clamp), and nudges steering through `ApplySteeringLocked` so USB reports always combine user input with FFB torque.
   - `DestroyUSBGadget()` still exists for manual cleanup but is no longer invoked during normal shutdown.

   ### `src/config.cpp`
   - Loads `/etc/wheel-emulator.conf`, generating a documented default if the file is missing.
   - Supported keys: `[devices] keyboard/mouse`, `[sensitivity] sensitivity` (clamped 1–100), `[ffb] gain` (0.1–4.0). `[button_mapping]` entries are reference-only; actual bindings stay hardcoded.
   - `SaveDefault` spells out axes, inverted pedals, button usage, and the Ctrl+M toggle so the file doubles as user-facing documentation.

   ---

   ## USB Gadget Lifecycle
   - The first launch builds the `g29wheel` ConfigFS tree, binds a UDC, and writes the Logitech descriptor. Subsequent launches reuse the exact same directory, UDC binding, VID/PID, and serial, which keeps games from re-learning devices.
   - Because the gadget stays bound even when the emulator is disabled or exited, the host always sees a stable Logitech wheel identity. Toggling only starts/stops streaming data.
   - Manual teardown (before unplugging modules or switching gadget stacks) requires:

     ```bash
     echo '' | sudo tee /sys/kernel/config/usb_gadget/g29wheel/UDC
     sudo rm -rf /sys/kernel/config/usb_gadget/g29wheel
     ```

     This mirrors `GamepadDevice::DestroyUSBGadget()` if you ever need to invoke it from inside the codebase.

   ---

   ## Enable/Disable Handshake
   1. **Enable path (`SetEnabled(true)`):** grab keyboard/mouse → resync keys if pending → capture a snapshot of every axis/button → zero `output_enabled`, `warmup_frames`, and `state_dirty` → apply neutral (without resetting FFB) and build its report → apply the snapshot and build its report → ensure the gadget endpoint is writable (`WaitForEndpointReady`) → write neutral then snapshot via `WriteReportBlocking` → start gadget threads if they were stopped → set `output_enabled=true`, `warmup_frames=25`, and wake `state_cv` so the asynchronous writer begins streaming.
   2. **Disable path (`SetEnabled(false)`):** stop warmup/output, set `state_dirty=false` → apply neutral with FFB reset → write that report synchronously → release all grabs. The UDC remains bound; games keep seeing a neutral G29 instead of a hotplug event.
   3. `main` also forces `SetEnabled(false)` if `Input::AllRequiredGrabbed()` drops to false, preventing half-updated reports when a device vanishes.

   ---

   ## Thread Model

   | Thread | Entry Point | Purpose | Notes |
   |--------|-------------|---------|-------|
   | Main | `main()` | Polls input, detects toggles, calls `ProcessInputFrame` | Also watches for device loss and handles shutdown |
   | Gadget Writer | `USBGadgetPollingThread()` | Sole HID IN writer | Sends frames when `state_dirty` or `warmup_frames` are set and `output_enabled` is true |
   | Gadget OUTPUT | `USBGadgetOutputThread()` | Reads 7-byte OUTPUT packets | Reassembles partial reads, feeds `ParseFFBCommand`, obeys `output_enabled` |
   | FFB Physics | `FFBUpdateThread()` | Computes torque offset | 125 Hz loop with damping, clamps, and `ApplySteeringLocked` |

   `state_mutex` protects shared state, while `state_cv`/`ffb_cv` provide wakeups. No other locks exist, so ordering is easy to reason about.

   ---

   ## HID Report Layout
   - `BuildHIDReportLocked()` outputs 13 bytes matching Logitech’s descriptor:
     1. Bytes 0–1: steering (ABS_X) signed 16-bit offset by +32768.
     2. Bytes 2–3: clutch (ABS_Y) inverted so rest=65535, pressed≈0.
     3. Bytes 4–5: throttle (ABS_Z) inverted.
     4. Bytes 6–7: brake (ABS_RZ) inverted.
     5. Byte 8 low nibble: D-pad encoded into the standard 0–7 hat values (0x0F when idle).
     6. Bytes 9–12: 26 little-endian button bits following Logitech’s order.
   - `SendNeutral(reset_ffb)` zeros axes, hat, and buttons, optionally clearing the FFB offset, and wakes the gadget writer so the host always receives a clean frame after a toggle or shutdown.

   ---

   ## Force Feedback Pipeline
   1. The host driver writes 7-byte OUTPUT reports over interface 0. `USBGadgetOutputThread` buffers them until it has one full packet.
   2. `ParseFFBCommand` handles the standard Logitech opcodes: 0x11 constant force slot, 0x13 stop, 0x14 enable autocenter, 0xf5 disable autocenter, 0xfe configure autocenter strength, 0xf8 extended commands (LEDs, range, etc.).
   3. Any change wakes `FFBUpdateThread` through `ffb_cv`.
   4. `FFBUpdateThread` runs ~125 Hz, clamps `dt` to 0.01 s, shapes torque (`ShapeFFBTorque`), adds autocenter torque, multiplies by `ffb_gain`, feeds the damped spring-mass model (stiffness 120, damping 8, velocity clamp 90 k/s, offset clamp ±22 k), and stores the latest `ffb_offset`/`ffb_velocity`.
   5. If applying the offset changes steering, `state_dirty` becomes true and the gadget writer immediately emits a new HID frame so games feel the torque without waiting for additional input.

   ---

   ## Controls & Mapping

   | Control | Input Source | HID Field |
   |---------|--------------|-----------|
   | Steering | Mouse X delta | ABS_X |
   | Throttle | `KEY_W` | ABS_Z (inverted) |
   | Brake | `KEY_S` | ABS_RZ (inverted) |
   | Clutch | `KEY_A` | ABS_Y (inverted) |
   | D-pad | Arrow keys | ABS_HAT0X / ABS_HAT0Y |
   | Buttons 1–26 | `Q,E,F,G,H,R,T,Y,U,I,O,P,1,2,3,4,5,6,7,8,9,0,LeftShift,Space,Tab,Enter` | Packed little-endian |

   Bindings are hardcoded in `CaptureSnapshot` / `ApplySnapshotLocked`. README lists the same table for quick reference.

   ---

   ## Configuration & Tuning
   - `[devices] keyboard/mouse`: leave blank for hotplug auto-detect or set to `/dev/input/eventX` paths to pin devices.
   - `[sensitivity] sensitivity`: integer 1–100. `ApplySteeringDeltaLocked` multiplies mouse delta by `sensitivity * 0.05` and clamps per-frame changes to ±2000 counts before clamping steering to ±32767.
   - `[ffb] gain`: floating point 0.1–4.0. Both the config parser and `SetFFBGain` clamp it to keep the physics loop stable. Higher values make the wheel feel heavier.
   - `[button_mapping]` is informational. Actual bindings live in code; the config section helps users remember what key drives each Logitech button.

   ---

   ## Reliability & Troubleshooting
   - Losing a grabbed keyboard or mouse while enabled logs an error and forces `SetEnabled(false)`, ensuring the host never receives half-updated frames.
   - `Input::ReleaseDeviceKeys` clears every pressed key when a device disappears, so games do not see stuck buttons after an unplug.
   - `GamepadDevice::Create()` prints actionable errors (ConfigFS mount, libcomposite, dummy_hcd, missing UDC) whenever gadget creation fails.
   - Verify enumeration with `lsusb | grep 046d:c24f`. The device should be present even when the emulator is “disabled” because the gadget remains bound and neutral.
   - To completely remove the gadget (e.g., before loading another gadget stack), run the manual cleanup commands shown above or call `DestroyUSBGadget()` from code.

   ---

   **Last verified:** November 2025 — matches the commit where UHID/uinput were removed, KEY_ENTER became button 26, and the gadget remains bound across toggles and process restarts. Keep this document in sync whenever bindings, gadget sequencing, or thread behavior change.
