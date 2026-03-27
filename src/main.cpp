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
#include <unistd.h>

MinecraftServer* globalServer = nullptr;
std::atomic<bool> isShuttingDown{false};

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
    pollfd stdinPoll{
        .fd = STDIN_FILENO,
        .events = POLLIN,
        .revents = 0,
    };

    std::string line;
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

        if (!std::getline(std::cin, line)) {
            Logger::info("Console input closed.");
            return;
        }

        if (!line.empty()) {
            server.addCommand(line);
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

        // Register signal handlers
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        // Use a polling console thread so shutdown stays graceful and joinable.
        std::jthread consoleThread(consoleLoop, std::ref(server));

        // Run server (blocks until stop)
        server.run();

        consoleThread.request_stop();
        globalServer = nullptr;

        Logger::info("Waiting for background threads to finish saving...");
    }
    return 0;
}
