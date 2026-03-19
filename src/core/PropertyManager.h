#pragma once

#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

class PropertyManager {
public:
    explicit PropertyManager(const std::string& filename) : filename_(filename) {
        if (std::filesystem::exists(filename_)) {
            load();
        } else {
            std::cerr << "[WARNING] " << filename_ << " does not exist" << std::endl;
            std::cout << "[INFO] Generating new properties file" << std::endl;
            save();
        }
    }

    std::string getStringProperty(const std::string& key, const std::string& defaultValue) {
        auto it = properties_.find(key);
        if (it == properties_.end()) {
            properties_[key] = defaultValue;
            save();
            return defaultValue;
        }
        return it->second;
    }

    int getIntProperty(const std::string& key, int defaultValue) {
        std::string val = getStringProperty(key, std::to_string(defaultValue));
        try {
            return std::stoi(val);
        } catch (...) {
            properties_[key] = std::to_string(defaultValue);
            return defaultValue;
        }
    }

    bool getBooleanProperty(const std::string& key, bool defaultValue) {
        std::string val = getStringProperty(key, defaultValue ? "true" : "false");
        return val == "true";
    }

private:
    std::string filename_;
    std::map<std::string, std::string> properties_;

    void load() {
        std::ifstream file(filename_);
        std::string line;
        while (std::getline(file, line)) {
            // Skip comments and empty lines
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string value = line.substr(eq + 1);
                // Trim
                while (!key.empty() && key.back() == ' ') key.pop_back();
                while (!value.empty() && value.front() == ' ') value.erase(value.begin());
                properties_[key] = value;
            }
        }
    }

    void save() {
        std::ofstream file(filename_);
        file << "#Minecraft server properties" << std::endl;
        for (const auto& [key, value] : properties_) {
            file << key << "=" << value << std::endl;
        }
    }
};
