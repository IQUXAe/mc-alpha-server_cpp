#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <charconv>

class PropertyManager {
public:
    explicit PropertyManager(std::string_view filename) : filename_(filename) {
        if (std::filesystem::exists(filename_)) {
            load();
        } else {
            std::cerr << "[WARNING] " << filename_ << " does not exist\n";
            std::cout << "[INFO] Generating new properties file\n";
            save();
        }
    }

    std::string getStringProperty(std::string_view key, std::string_view defaultValue) {
        auto it = properties_.find(std::string(key));
        if (it == properties_.end()) {
            properties_.emplace(key, defaultValue);
            save();
            return std::string(defaultValue);
        }
        return it->second;
    }

    int getIntProperty(std::string_view key, int defaultValue) {
        auto val = getStringProperty(key, std::to_string(defaultValue));
        int result = defaultValue;
        auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), result);
        if (ec != std::errc{}) {
            properties_[std::string(key)] = std::to_string(defaultValue);
            return defaultValue;
        }
        return result;
    }

    bool getBooleanProperty(std::string_view key, bool defaultValue) {
        return getStringProperty(key, defaultValue ? "true" : "false") == "true";
    }

private:
    std::string filename_;
    std::unordered_map<std::string, std::string> properties_;

    void load() {
        std::ifstream file(filename_);
        for (std::string line; std::getline(file, line); ) {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            auto key = std::string_view(line).substr(0, eq);
            auto val = std::string_view(line).substr(eq + 1);
            while (!key.empty() && key.back() == ' ') key.remove_suffix(1);
            while (!val.empty() && val.front() == ' ') val.remove_prefix(1);
            properties_.emplace(key, val);
        }
    }

    void save() {
        std::ofstream file(filename_);
        file << "#Minecraft server properties\n";
        for (const auto& [key, value] : properties_)
            file << key << '=' << value << '\n';
    }
};
