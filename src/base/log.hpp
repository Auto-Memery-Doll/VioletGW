#pragma once

#include <spdlog/spdlog.h>
#include <string>

#ifndef VGW_IO_TRACE
#define VGW_IO_TRACE 0
#endif

// Datapath / I/O path logs. Compiled out unless -DVGW_IO_TRACE=ON.
#if VGW_IO_TRACE
#define VGW_IO_LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define VGW_IO_LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#else
#define VGW_IO_LOG_INFO(...) ((void)0)
#define VGW_IO_LOG_WARN(...) ((void)0)
#endif

namespace vgw {

/** Initialize default console logger; optional rotating file sink. */
void init_logging(const std::string& log_file = "");

}  // namespace vgw
