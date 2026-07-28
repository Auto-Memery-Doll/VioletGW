#include "base/log.hpp"

#include <memory>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace vgw {

void init_logging(const std::string& log_file) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    if (!log_file.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file, 10 * 1024 * 1024, 3));
    }

    auto logger = std::make_shared<spdlog::logger>("vgw", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::info);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::warn);
}

}  // namespace vgw
