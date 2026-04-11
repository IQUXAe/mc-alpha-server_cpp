#include "Logger.h"
#include <iomanip>
#include <sstream>
#include <mutex>
#include <unistd.h>

LogLevel Logger::minLevel_ = LogLevel::INFO;
std::function<std::string()> Logger::consoleLineProvider_;

namespace {
std::mutex gLogMutex;
bool gStdoutIsTTY = ::isatty(STDOUT_FILENO) != 0;

void clearConsoleLine(std::ostream& out) {
    if (gStdoutIsTTY) {
        out << "\r\33[2K\r";
    }
}
}

void Logger::setConsoleLineProvider(std::function<std::string()> provider) {
    std::lock_guard lock(gLogMutex);
    consoleLineProvider_ = std::move(provider);
}

void Logger::refreshConsoleLine() {
    std::lock_guard lock(gLogMutex);
    if (!consoleLineProvider_) {
        return;
    }

    clearConsoleLine(std::cout);
    std::cout << consoleLineProvider_() << std::flush;
}

void Logger::log(LogLevel level, const std::string& msg) {
    if (level < minLevel_) return;

    std::lock_guard lock(gLogMutex);

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
    clearConsoleLine(out);
    out << oss.str() << '\n';
    if (consoleLineProvider_) {
        std::cout << consoleLineProvider_() << std::flush;
    }
}
