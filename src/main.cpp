#include "MinecraftServer.h"
#include "core/Logger.h"

#include <thread>
#include <string>
#include <csignal>
#include <atomic>
#include <cstdlib>
#include <cerrno>
#include <poll.h>
#include <iostream>
#include <mutex>
#include <termios.h>
#include <cctype>
#include <unistd.h>

MinecraftServer* globalServer = nullptr;
std::atomic<bool> isShuttingDown{false};
std::mutex gConsoleLineMutex;
std::string gConsoleLine;

void signalHandler(int signum) {
    if (globalServer && !isShuttingDown.exchange(true)) {
        Logger::info("Interrupt signal ({}) received. Stopping server gracefully...", signum);
        globalServer->stop();
    } else {
        Logger::info("Force killing server...");
        std::_Exit(signum);
    }
}

void consoleLoop(std::stop_token stopToken, MinecraftServer& server) {
    struct TerminalModeGuard {
        bool active = false;
        termios original{};

        ~TerminalModeGuard() {
            if (active) {
                ::tcsetattr(STDIN_FILENO, TCSANOW, &original);
            }
        }
    } terminalMode;

    const bool useRawConsole = ::isatty(STDIN_FILENO) != 0;
    if (useRawConsole) {
        if (::tcgetattr(STDIN_FILENO, &terminalMode.original) == 0) {
            termios raw = terminalMode.original;
            raw.c_lflag &= ~(ICANON | ECHO);
            raw.c_iflag &= ~(IXON | ICRNL);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            if (::tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
                terminalMode.active = true;
            }
        }
    }

    pollfd stdinPoll{
        .fd = STDIN_FILENO,
        .events = POLLIN,
        .revents = 0,
    };

    std::string line;
    Logger::refreshConsoleLine();
    while (!stopToken.stop_requested()) {
        const int pollResult = ::poll(&stdinPoll, 1, 200);
        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }

            Logger::warning("Console input polling failed, disabling console reader.");
            return;
        }

        if (pollResult == 0) {
            continue;
        }

        if ((stdinPoll.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            Logger::info("Console input closed.");
            return;
        }

        if ((stdinPoll.revents & POLLIN) == 0) {
            continue;
        }

        if (!useRawConsole || !terminalMode.active) {
            if (!std::getline(std::cin, line)) {
                Logger::info("Console input closed.");
                return;
            }

            if (!line.empty()) {
                server.addCommand(line);
            }
            continue;
        }

        char ch = '\0';
        const ssize_t readBytes = ::read(STDIN_FILENO, &ch, 1);
        if (readBytes <= 0) {
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            {
                std::lock_guard lock(gConsoleLineMutex);
                line.swap(gConsoleLine);
                gConsoleLine.clear();
            }
            Logger::refreshConsoleLine();
            if (!line.empty()) {
                server.addCommand(line);
                line.clear();
            }
            continue;
        }

        if (ch == 127 || ch == '\b') {
            {
                std::lock_guard lock(gConsoleLineMutex);
                if (!gConsoleLine.empty()) {
                    gConsoleLine.pop_back();
                }
            }
            Logger::refreshConsoleLine();
            continue;
        }

        if (std::isprint(static_cast<unsigned char>(ch)) != 0) {
            {
                std::lock_guard lock(gConsoleLineMutex);
                gConsoleLine.push_back(ch);
            }
            Logger::refreshConsoleLine();
        }
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::cout << "  ___  _      _         ___                      " << std::endl;
    std::cout << " / _ \\| |_ __| |_  __ _/ __| ___ _ ___ _____ _ _ " << std::endl;
    std::cout << "| (_) | | '_ \\ ' \\/ _` \\__ \\/ -_) '_\\ V / -_) '_|" << std::endl;
    std::cout << " \\___/|_| .__/_||_\\__,_|___/\\___|_|  \\_/\\___|_|  " << std::endl;
    std::cout << "        |_|  Minecraft Alpha 1.2.6 - C++ Edition " << std::endl;
    std::cout << std::endl;

    // Limit scope so destructors are called BEFORE _Exit()
    {
        MinecraftServer server;
        globalServer = &server;
        Logger::setConsoleLineProvider([] {
            std::lock_guard lock(gConsoleLineMutex);
            return std::string("> ") + gConsoleLine;
        });

        // Register signal handlers
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        // Use a polling console thread so shutdown stays graceful and joinable.
        std::jthread consoleThread(consoleLoop, std::ref(server));

        // Run server (blocks until stop)
        server.run();

        consoleThread.request_stop();
        Logger::setConsoleLineProvider({});
        globalServer = nullptr;

        Logger::info("Waiting for background threads to finish saving...");
    }
    return 0;
}
