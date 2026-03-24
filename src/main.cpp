#include "MinecraftServer.h"
#include "core/Logger.h"

#include <thread>
#include <string>
#include <csignal>
#include <atomic>
#include <cstdlib>

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

int main(int argc, char* argv[]) {
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

        // Start console reader thread
        std::thread consoleThread([&server]() {
            std::string line;
            while (std::getline(std::cin, line)) {
                if (!line.empty()) {
                    server.addCommand(line);
                }
            }
        });
        consoleThread.detach();

        // Run server (blocks until stop)
        server.run();

        globalServer = nullptr;
        
        Logger::info("Waiting for background threads to finish saving...");
    }

    // Use _Exit instead of exit to bypass the std::cin mutex deadlock,
    // which is still held by the sleeping detached consoleThread.
    std::_Exit(0); // its just work
}