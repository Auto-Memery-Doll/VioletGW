#include "vgw.h"

#include <gflags/gflags.h>

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, /*remove_flags=*/true);
    vgw::VioletGW app;
    if (const int rc = app.init(argc, argv)) {
        return rc;
    }
    return app.run();
}
