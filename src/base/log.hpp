#pragma once

#include <spdlog/spdlog.h>
#include <string>

namespace vgw {

/** Initialize default console logger; optional rotating file sink. */
void init_logging(const std::string& log_file = "");

}  // namespace vgw

