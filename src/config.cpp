#include "config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/input-event-codes.h>

bool Config::Load() {
    // Only use system config at /etc/wheel-emulator.conf
    const char* system_config = "/etc/wheel-emulator.conf";
    if (LoadFromFile(system_config)) {
        std::cout << "Loaded config from: " << system_config << std::endl;
        return true;
    }
    
    // Generate default config in /etc
    std::cout << "No config found, generating default at " << system_config << std::endl;
    SaveDefault(system_config);
    std::cout << "Default config saved. Please edit " << system_config << " and run --detect to configure devices." << std::endl;
    
    // Set default values
    sensitivity = 50;
    
    // Set default button mappings (joystick style for wheel)
    button_map["KEY_Q"] = BTN_TRIGGER;
    button_map["KEY_E"] = BTN_THUMB;
    button_map["KEY_F"] = BTN_THUMB2;
    button_map["KEY_G"] = BTN_TOP;
    button_map["KEY_H"] = BTN_TOP2;
    button_map["KEY_R"] = BTN_PINKIE;
    button_map["KEY_T"] = BTN_BASE;
    button_map["KEY_Y"] = BTN_BASE2;
    button_map["KEY_U"] = BTN_BASE3;
    button_map["KEY_I"] = BTN_BASE4;
    button_map["KEY_O"] = BTN_BASE5;
    button_map["KEY_P"] = BTN_BASE6;
    
    return true;
}

bool Config::LoadFromFile(const char* path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    ParseINI(buffer.str());
    
    return true;
}

void Config::ParseINI(const std::string& content) {
    std::istringstream stream(content);
    std::string line;
    std::string section;
    
    while (std::getline(stream, line)) {
        // Remove whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // Check for section
        if (line[0] == '[' && line[line.length() - 1] == ']') {
            section = line.substr(1, line.length() - 2);
            continue;
        }
        
        // Parse key=value
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }
        
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);
        
        // Trim key and value
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        if (section == "devices") {
            if (key == "keyboard") {
                keyboard_device = value;
            } else if (key == "mouse") {
                mouse_device = value;
            }
        } else if (section == "sensitivity") {
            if (key == "sensitivity") {
                int val = std::stoi(value);
                // Clamp to valid range
                if (val < 1) val = 1;
                if (val > 100) val = 100;
                sensitivity = val;
            }
        } else if (section == "button_mapping") {
            // Map button code to key name (format: BUTTON=KEY)
            int button_code = -1;
            if (key == "BTN_TRIGGER") button_code = BTN_TRIGGER;
            else if (key == "BTN_THUMB") button_code = BTN_THUMB;
            else if (key == "BTN_THUMB2") button_code = BTN_THUMB2;
            else if (key == "BTN_TOP") button_code = BTN_TOP;
            else if (key == "BTN_TOP2") button_code = BTN_TOP2;
            else if (key == "BTN_PINKIE") button_code = BTN_PINKIE;
            else if (key == "BTN_BASE") button_code = BTN_BASE;
            else if (key == "BTN_BASE2") button_code = BTN_BASE2;
            else if (key == "BTN_BASE3") button_code = BTN_BASE3;
            else if (key == "BTN_BASE4") button_code = BTN_BASE4;
            else if (key == "BTN_BASE5") button_code = BTN_BASE5;
            else if (key == "BTN_BASE6") button_code = BTN_BASE6;
            else if (key == "BTN_DEAD") button_code = BTN_DEAD;
            
            if (button_code != -1) {
                button_map[value] = button_code;
            }
        }
    }
}

void Config::SaveDefault(const char* path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to create config file: " << path << std::endl;
        return;
    }
    
    file << "# Wheel Emulator Configuration\n";
    file << "# Run with --detect flag to identify your devices\n\n";
    
    file << "[devices]\n";
    file << "# Specify exact device paths (use --detect to find them)\n";
    file << "# Leave empty for auto-detection\n";
    file << "keyboard=\n";
    file << "mouse=\n\n";
    
    file << "[sensitivity]\n";
    file << "sensitivity=50\n\n";
    
    file << "[controls]\n";
    file << "# Logitech G29 Racing Wheel Controls\n";
    file << "# Format: CONTROL=KEYBOARD_KEY or MOUSE_BUTTON\n\n";
    file << "# Primary Controls (Hardcoded)\n";
    file << "# Steering: Mouse horizontal movement\n";
    file << "# Throttle: Hold KEY_W to increase (0-100%)\n";
    file << "# Brake: Hold KEY_S to increase (0-100%)\n";
    file << "# D-Pad: Arrow keys (KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT)\n\n";
    
    file << "[button_mapping]\n";
    file << "# Logitech G29 Racing Wheel - Optimized for Assetto Corsa\n";
    file << "# Format: EMULATED_BUTTON=KEYBOARD_KEY\n";
    file << "# All 25 buttons mapped for maximum functionality\n\n";
    
    file << "# === ESSENTIAL DRIVING CONTROLS ===\n";
    file << "BTN_TRIGGER=KEY_Q          # Shift Down (paddle/sequential)\n";
    file << "BTN_THUMB=KEY_E            # Shift Up (paddle/sequential)\n";
    file << "BTN_THUMB2=KEY_SPACE       # Handbrake / E-Brake\n";
    file << "BTN_TOP=KEY_A              # Look Left\n";
    file << "BTN_TOP2=KEY_D             # Look Right\n";
    file << "BTN_PINKIE=KEY_F           # Flash Lights / High Beams (quick toggle)\n";
    file << "BTN_BASE=KEY_R             # Toggle Headlights\n";
    file << "BTN_BASE2=KEY_G            # Horn\n\n";
    
    file << "# === CAMERA & VIEW ===\n";
    file << "BTN_BASE3=KEY_C            # Change Camera View\n";
    file << "BTN_BASE4=KEY_V            # Change HUD / Dashboard View\n";
    file << "BTN_BASE5=KEY_ENTER        # Confirm / Select (menu navigation)\n";
    file << "BTN_BASE6=KEY_ESC          # Pause / Back / Cancel\n\n";
    
    file << "# === PIT & RACE CONTROLS ===\n";
    file << "BTN_DEAD=KEY_F1            # Pit Limiter\n";
    file << "BTN_TRIGGER_HAPPY1=KEY_F2  # Request Pit Stop / Enter Pits\n";
    file << "BTN_TRIGGER_HAPPY2=KEY_T   # Cycle Tire Display / Telemetry\n";
    file << "BTN_TRIGGER_HAPPY3=KEY_TAB # Leaderboard / Standings\n\n";
    
    file << "# === ASSISTS & SETUP ===\n";
    file << "BTN_TRIGGER_HAPPY4=KEY_F5  # TC (Traction Control) Decrease\n";
    file << "BTN_TRIGGER_HAPPY5=KEY_F6  # TC Increase\n";
    file << "BTN_TRIGGER_HAPPY6=KEY_F7  # ABS Decrease\n";
    file << "BTN_TRIGGER_HAPPY7=KEY_F8  # ABS Increase\n\n";
    
    file << "# === UTILITY FUNCTIONS ===\n";
    file << "BTN_TRIGGER_HAPPY8=KEY_I   # Ignition / Engine Start\n";
    file << "BTN_TRIGGER_HAPPY9=KEY_F9  # Screenshot\n";
    file << "BTN_TRIGGER_HAPPY10=KEY_F12 # Save Replay\n";
    file << "BTN_TRIGGER_HAPPY11=KEY_F10 # Reset Car to Track (far from common keys)\n\n";
    
    file << "# === RESERVED / UNASSIGNED ===\n";
    file << "# BTN_TRIGGER_HAPPY12=      # (Reserved for future use)\n\n";
    
    file << "# === AXES (Read-only, automatically handled) ===\n";
    file << "# ABS_X: Steering wheel (-32768 to 32767, mouse horizontal)\n";
    file << "# ABS_Y: Unused (always 0)\n";
    file << "# ABS_Z: Brake pedal (0 to 255, KEY_S hold percentage)\n";
    file << "# ABS_RZ: Throttle pedal (0 to 255, KEY_W hold percentage)\n";
    file << "# ABS_HAT0X: D-Pad horizontal (-1, 0, 1) - Menu navigation LEFT/RIGHT\n";
    file << "# ABS_HAT0Y: D-Pad vertical (-1, 0, 1) - Menu navigation UP/DOWN\n";
}

bool Config::UpdateDevices(const std::string& kbd_path, const std::string& mouse_path) {
    const char* config_path = "/etc/wheel-emulator.conf";
    
    // Read existing config
    std::ifstream infile(config_path);
    if (!infile.is_open()) {
        std::cerr << "Failed to open config for updating: " << config_path << std::endl;
        return false;
    }
    
    std::vector<std::string> lines;
    std::string line;
    std::string current_section;
    bool updated_keyboard = false;
    bool updated_mouse = false;
    
    while (std::getline(infile, line)) {
        // Track current section
        if (!line.empty() && line[0] == '[') {
            current_section = line;
            lines.push_back(line);
            continue;
        }
        
        // Update device lines in [devices] section
        if (current_section.find("[devices]") != std::string::npos) {
            if (line.find("keyboard=") != std::string::npos) {
                lines.push_back("keyboard=" + kbd_path);
                updated_keyboard = true;
                continue;
            } else if (line.find("mouse=") != std::string::npos) {
                lines.push_back("mouse=" + mouse_path);
                updated_mouse = true;
                continue;
            }
        }
        
        lines.push_back(line);
    }
    infile.close();
    
    // Write updated config
    std::ofstream outfile(config_path);
    if (!outfile.is_open()) {
        std::cerr << "Failed to write updated config: " << config_path << std::endl;
        return false;
    }
    
    for (const auto& l : lines) {
        outfile << l << "\n";
    }
    outfile.close();
    
    if (updated_keyboard && updated_mouse) {
        std::cout << "\nConfig updated successfully at " << config_path << std::endl;
        return true;
    } else {
        std::cerr << "Warning: Could not find device entries in config" << std::endl;
        return false;
    }
}
