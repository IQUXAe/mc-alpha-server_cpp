#include "MinecraftServer.h"

#include <iostream>
#include <thread>
#include <string>

int main(int argc, char* argv[]) {
    std::cout << "  ___  _      _         ___                      " << std::endl;
    std::cout << " / _ \\| |_ __| |_  __ _/ __| ___ _ ___ _____ _ _ " << std::endl;
    std::cout << "| (_) | | '_ \\ ' \\/ _` \\__ \\/ -_) '_\\ V / -_) '_|" << std::endl;
    std::cout << " \\___/|_| .__/_||_\\__,_|___/\\___|_|  \\_/\\___|_|  " << std::endl;
    std::cout << "        |_|  Minecraft Alpha 1.2.6 - C++ Edition " << std::endl;
    std::cout << std::endl;

    MinecraftServer server;

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

    return 0;
}
