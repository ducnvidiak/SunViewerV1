#pragma once
#include <string>

namespace common {

	void log_info(const std::string& msg);
	void log_warn(const std::string& msg);
	void log_error(const std::string& msg);

}