# Wheel HID Emulator

**Keyboard + mouse as a virtual steering wheel with Force Feedback.**

## Requirements

- [vJoy Driver](https://github.com/jshafer817/vJoy/releases) installed

## How to Run

1. Download `wheel-emulator.exe` from [Releases](https://github.com/dewdgi/wheel-hid-emulator/releases).
2. Run it. Press **Ctrl + M** to toggle emulation.

## Controls

| Action | Input | vJoy Axis |
| :--- | :--- | :--- |
| **Steer** | Mouse X | Axis X |
| **Throttle** | `W` | Axis Z |
| **Brake** | `S` | Axis Rz |
| **Clutch** | `A` | Axis Y |
| **D-Pad** | Arrows | Hat Switch |

**Buttons:**
`Q, E, F, G, H, R, T, Y` map to Buttons 1-8.

## Configuration

`wheel-emulator.conf` (auto-generated if missing):

```ini
[sensitivity]
sensitivity=50    # 1-100. Higher = faster steering.

[ffb]
gain=1.0          # 0.1-4.0. Force Feedback strength.
```

## Building from Source

Requires **MinGW-w64** (g++) on PATH.

```
build_with_g++.bat
```

## License

MIT. See [LICENSE](LICENSE).

Uses [vJoy](https://github.com/shauleiz/vJoy) SDK (MIT). See [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES).

---

Architecture details: [`logics.md`](logics.md)
