#include "vgw.h"

int main(int argc, char** argv) {
    vgw::VioletGW app;
    if (const int rc = app.init(argc, argv)) {
        return rc;
    }
    return app.run();
}
