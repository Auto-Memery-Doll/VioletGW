#include "cp_shm.hpp"

#include <cstdio>

/** Manual: Go vgwcp publish → C++ CpShm::poll_apply. Not part of ctest. */
int main(int argc, char** argv) {
    const char* name = argc > 1 ? argv[1] : "/vgw_cp_verify";
    auto shm =
        vgw::control::CpShm::open(name, /*create_if_missing=*/false, nullptr);
    if (shm == nullptr) {
        std::fprintf(stderr, "open %s failed\n", name);
        return 1;
    }
    vgw::upstream::UpstreamTable table;
    if (!shm->poll_apply(&table)) {
        std::fprintf(stderr, "poll_apply failed (version=%u)\n",
                     shm->raw() ? shm->raw()->version : 0);
        return 1;
    }
    std::printf("cpp apply OK size=%zu version=%u\n", table.size(),
                shm->last_applied_version());
    return table.size() > 0 ? 0 : 1;
}
