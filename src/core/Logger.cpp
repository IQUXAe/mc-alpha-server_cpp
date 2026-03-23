#include "Logger.h"
#include <iomanip>
#include <sstream>

LogLevel Logger::minLevel_ = LogLevel::INFO;

void Logger::log(LogLevel level, const std::string& msg) {
    if (level < minLevel_) return;

    const char* prefix = [&] {
        switch (level) {
            case LogLevel::DEBUG:   return "[D]";
            case LogLevel::INFO:    return "[INFO]";
            case LogLevel::WARNING: return "[WARNING]";
            case LogLevel::SEVERE:  return "[SEVERE]";
        }
        return "[?]";
    }();

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    oss << std::format(".{:03d} {} {}", ms.count(), prefix, msg);

    auto& out = (level >= LogLevel::WARNING) ? std::cerr : std::cout;
    out << oss.str() << '\n';
}
