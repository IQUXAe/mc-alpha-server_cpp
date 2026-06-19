#include "Logger.h"
#include <iomanip>
#include <sstream>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;

std::atomic<LogLevel> Logger::minLevel_{LogLevel::INFO};

namespace {
std::mutex gLogMutex;
std::queue<std::string> gLogQueue;
std::condition_variable_any gLogCv;
bool gStdoutIsTTY = ::isatty(STDOUT_FILENO) != 0;
std::function<std::string()> gConsoleLineProvider;

void clearConsoleLine(std::ostream& out) {
    if (gStdoutIsTTY) {
        out << "\r\33[2K\r";
    }
}

void flusherThread(std::stop_token st) {
    while (!st.stop_requested()) {
        std::queue<std::string> toWrite;
        {
            std::unique_lock lock(gLogMutex);
            gLogCv.wait(lock, st, [&] { return !gLogQueue.empty(); });
            if (st.stop_requested() && gLogQueue.empty()) return;
            toWrite.swap(gLogQueue);
        }

        while (!toWrite.empty()) {
            std::string& msg = toWrite.front();
            auto& out = (msg.starts_with("[SEVERE]") || msg.starts_with("[WARNING]")) ? std::cerr : std::cout;
            clearConsoleLine(out);
            out << msg << '\n';
            toWrite.pop();
        }

        {
            std::lock_guard lock(gLogMutex);
            if (gConsoleLineProvider) {
                clearConsoleLine(std::cout);
                std::cout << gConsoleLineProvider() << std::flush;
            }
        }
    }
}

std::jthread& getFlusher() {
    static std::jthread flusher(flusherThread);
    return flusher;
}

std::string formatMessage(LogLevel level, const std::string& msg) {
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
    return oss.str();
}

}

void Logger::setConsoleLineProvider(std::function<std::string()> provider) {
    std::lock_guard lock(gLogMutex);
    gConsoleLineProvider = std::move(provider);
}

void Logger::refreshConsoleLine() {
    std::lock_guard lock(gLogMutex);
    if (!gConsoleLineProvider) return;
    clearConsoleLine(std::cout);
    std::cout << gConsoleLineProvider() << std::flush;
}

void Logger::log(LogLevel level, const std::string& msg) {
    if (level < minLevel_) return;

    getFlusher();
    auto formatted = formatMessage(level, msg);

    std::lock_guard lock(gLogMutex);
    gLogQueue.push(std::move(formatted));
    gLogCv.notify_one();
}
