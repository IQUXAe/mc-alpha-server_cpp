#pragma once

#include <string>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdint>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    SEVERE
};

class Logger {
public:
    static void setLevel(LogLevel level) { minLevel_ = level; }
    static LogLevel getLevel() { return minLevel_; }

    static void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
    static void info(const std::string& msg) { log(LogLevel::INFO, msg); }
    static void warning(const std::string& msg) { log(LogLevel::WARNING, msg); }
    static void severe(const std::string& msg) { log(LogLevel::SEVERE, msg); }

    // Templated versions for format string support
    template<typename... Args>
    static void debug(const std::string& format, Args... args) {
        logFormatted(LogLevel::DEBUG, format, args...);
    }

    template<typename... Args>
    static void info(const std::string& format, Args... args) {
        logFormatted(LogLevel::INFO, format, args...);
    }

    template<typename... Args>
    static void warning(const std::string& format, Args... args) {
        logFormatted(LogLevel::WARNING, format, args...);
    }

    template<typename... Args>
    static void severe(const std::string& format, Args... args) {
        logFormatted(LogLevel::SEVERE, format, args...);
    }

private:
    static LogLevel minLevel_;

    static void log(LogLevel level, const std::string& msg);

    template<typename... Args>
    static void logFormatted(LogLevel level, const std::string& format, Args... args) {
        std::ostringstream oss;
        formatString(oss, format, args...);
        log(level, oss.str());
    }

    // Recursive base case
    static void formatString(std::ostringstream& oss, const std::string& format) {
        oss << format;
    }

    // Format string with {} placeholders
    template<typename T, typename... Args>
    static void formatString(std::ostringstream& oss, const std::string& format, T value, Args... args) {
        auto pos = format.find("{}");
        if (pos == std::string::npos) {
            oss << format << value;
            formatString(oss, std::string(), args...);
            return;
        }
        oss << format.substr(0, pos) << value;
        formatString(oss, format.substr(pos + 2), args...);
    }
};
