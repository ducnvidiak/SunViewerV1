#include "logger.h"
#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace {

    std::mutex g_mutex;

    std::string now() {
        using namespace std::chrono;

        auto t = system_clock::to_time_t(system_clock::now());
        std::tm tm{};
        localtime_s(&tm, &t);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%H:%M:%S");
        return oss.str();
    }

    void log_impl(const char* lvl, const std::string& msg) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::cout << "[" << now() << "] [" << lvl << "] " << msg << std::endl;
    }

}

namespace common {

    void log_info(const std::string& msg) { log_impl("INFO", msg); }
    void log_warn(const std::string& msg) { log_impl("WARN", msg); }
    void log_error(const std::string& msg) { log_impl("ERROR", msg); }

}