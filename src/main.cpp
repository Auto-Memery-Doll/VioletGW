#include "vgw.h"

#include "dpdk/datapath_flags.hpp"

#include <vector>

int main(int argc, char** argv) {
    std::vector<char*> eal_argv;
    vgw::parse_vgw_command_line_flags(argc, argv, &eal_argv);
    vgw::VioletGW app;
    if (const int rc = app.init(static_cast<int>(eal_argv.size()), eal_argv.data())) {
        return rc;
    }
    return app.run();
}
