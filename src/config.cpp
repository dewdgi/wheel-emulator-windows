#include "config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/input-event-codes.h>

bool Config::Load() {
    // Try user config first
    const char* home = getenv("HOME");
    if (home) {
        std::string user_config = std::string(home) + "/.config/wheel-emulator.conf";
        if (LoadFromFile(user_config.c_str())) {
            std::cout << "Loaded config from: " << user_config << std::endl;
            return true;
        }
    }
    
    // Try system config
    const char* system_config = "/etc/wheel-emulator.conf";
    if (LoadFromFile(system_config)) {
        std::cout << "Loaded config from: " << system_config << std::endl;
        return true;
    }
    
    // Generate default config
    std::cout << "No config found, generating default..." << std::endl;
    if (home) {
        std::string config_dir = std::string(home) + "/.config";
        mkdir(config_dir.c_str(), 0755);
        
        std::string user_config = config_dir + "/wheel-emulator.conf";
        SaveDefault(user_config.c_str());
        std::cout << "Default config saved to: " << user_config << std::endl;
    }
    
    // Set default button mappings
    button_map["KEY_Q"] = BTN_A;
    button_map["KEY_E"] = BTN_B;
    button_map["KEY_F"] = BTN_X;
    button_map["KEY_G"] = BTN_Y;
    button_map["KEY_H"] = BTN_TL;
    
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
        
        if (section == "sensitivity") {
            if (key == "sensitivity") {
                int val = std::stoi(value);
                // Clamp to valid range
                if (val < 1) val = 1;
                if (val > 100) val = 100;
                sensitivity = val;
            }
        } else if (section == "button_mapping") {
            // Map key name to button code
            int button_code = -1;
            if (value == "BTN_A") button_code = BTN_A;
            else if (value == "BTN_B") button_code = BTN_B;
            else if (value == "BTN_X") button_code = BTN_X;
            else if (value == "BTN_Y") button_code = BTN_Y;
            else if (value == "BTN_TL") button_code = BTN_TL;
            else if (value == "BTN_TR") button_code = BTN_TR;
            else if (value == "BTN_SELECT") button_code = BTN_SELECT;
            else if (value == "BTN_START") button_code = BTN_START;
            
            if (button_code != -1) {
                button_map[key] = button_code;
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
    
    file << "# Wheel Emulator Configuration\n\n";
    file << "[sensitivity]\n";
    file << "sensitivity=20\n\n";
    file << "[button_mapping]\n";
    file << "KEY_Q=BTN_A\n";
    file << "KEY_E=BTN_B\n";
    file << "KEY_F=BTN_X\n";
    file << "KEY_G=BTN_Y\n";
    file << "KEY_H=BTN_TL\n";
    file << "# KEY_R=BTN_TR\n";
    file << "# KEY_TAB=BTN_SELECT\n";
    file << "# KEY_ENTER=BTN_START\n";
}
