#pragma once

#include <spdlog/spdlog.h>
#include <string>

namespace vgm {

/** Initialize default console logger; optional rotating file sink. */
void init_logging(const std::string& log_file = "");

}  // namespace vgm

