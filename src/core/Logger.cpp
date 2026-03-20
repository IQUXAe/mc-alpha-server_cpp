#include "Logger.h"

LogLevel Logger::minLevel_ = LogLevel::INFO;  // Default to INFO, can be changed via config

void Logger::log(LogLevel level, const std::string& msg) {
    if (level < minLevel_) return;

    const char* prefix = "";

    switch (level) {
        case LogLevel::DEBUG:   prefix = "[D]"; break;
        case LogLevel::INFO:    prefix = "[INFO]"; break;
        case LogLevel::WARNING: prefix = "[WARNING]"; break;
        case LogLevel::SEVERE:  prefix = "[SEVERE]"; break;
    }

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    oss << " " << prefix << " " << msg;

    if (level == LogLevel::SEVERE || level == LogLevel::WARNING) {
        std::cerr << oss.str() << std::endl;
    } else {
        std::cout << oss.str() << std::endl;
    }
}
