#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <map>

class Config {
public:
    int sensitivity = 20;
    std::map<std::string, int> button_map;
    
    // Load configuration from default locations
    // Returns true if successful, false otherwise
    bool Load();
    
    // Save default configuration to specified path
    void SaveDefault(const char* path);

private:
    bool LoadFromFile(const char* path);
    void ParseINI(const std::string& content);
};

#endif // CONFIG_H
