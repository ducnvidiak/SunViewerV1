#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "app.h"
#include "config.h"
#include "logger.h"

int main()
{
    common::log_info("Starting Sender...");

    sender::App app;

    if (!app.run(common::config::kDefaultPort)) {
        common::log_error("Sender failed");
        return 1;
    }

    common::log_info("Sender stopped");
    return 0;
}