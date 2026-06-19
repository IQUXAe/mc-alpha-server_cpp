#pragma once

#include <string>
#include <format>
#include <iostream>
#include <chrono>
#include <cstdint>
#include <functional>
#include <atomic>

enum class LogLevel { DEBUG, INFO, WARNING, SEVERE };

class Logger {
public:
    static void setLevel(LogLevel level) { minLevel_.store(level, std::memory_order_relaxed); }
    [[nodiscard]] static LogLevel getLevel() { return minLevel_.load(std::memory_order_relaxed); }
    static void setConsoleLineProvider(std::function<std::string()> provider);
    static void refreshConsoleLine();

    template<typename... Args>
    static void debug(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::DEBUG, std::format(fmt, std::forward<Args>(args)...));
    }
    template<typename... Args>
    static void info(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::INFO, std::format(fmt, std::forward<Args>(args)...));
    }
    template<typename... Args>
    static void warning(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::WARNING, std::format(fmt, std::forward<Args>(args)...));
    }
    template<typename... Args>
    static void severe(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::SEVERE, std::format(fmt, std::forward<Args>(args)...));
    }

private:
    static std::atomic<LogLevel> minLevel_;
    static void log(LogLevel level, const std::string& msg);
};
