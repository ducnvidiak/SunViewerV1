#include "app.h"
#include "logger.h"
#include <iostream>

int main(int argc, char* argv[])
{
    std::string ip = "127.0.0.1";
    //std::string ip = "10.133.82.94";
    uint16_t port = 5000;

    if (argc >= 2)
        ip = argv[1];

    if (argc >= 3)
        port = static_cast<uint16_t>(std::stoi(argv[2]));

    common::log_info("Starting Viewer");

    viewer::App app;

    if (!app.run(ip, port)) {
        common::log_error("Viewer failed");
        return 1;
    }

    return 0;
}
