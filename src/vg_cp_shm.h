#pragma once

/*
 * Shared layout for flow_gateway control plane ↔ data plane.
 * Keep this header pure C so Go cgo can include it.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VG_CP_SHM_MAGIC 0x46474350u /* 'FGCP' */
#define VG_CP_SHM_MAX_EP 64
#define VG_CP_SHM_DEFAULT_NAME "/flow_gateway_cp"

#define VG_CP_POLICY_MOD 0
#define VG_CP_POLICY_RR 1

struct vg_cp_endpoint {
    uint32_t ip_be;
    uint16_t port; /* host byte order */
    uint16_t _pad;
};

struct vg_cp_shm {
    uint32_t magic;
    uint32_t version; /* bump last after a full publish */
    uint8_t policy;   /* VG_CP_POLICY_* */
    uint8_t flags;
    uint16_t count;
    struct vg_cp_endpoint endpoints[VG_CP_SHM_MAX_EP];
};

#ifdef __cplusplus
}
#endif

