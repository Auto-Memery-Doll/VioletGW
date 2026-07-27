#pragma once

/*
 * Shared layout for VGW control plane ↔ data plane.
 * Keep this header pure C so Go cgo can include it.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VGW_CP_SHM_MAGIC 0x56475743u /* 'VGWC' */
#define VGW_CP_SHM_MAX_EP 64
#define VGW_CP_SHM_DEFAULT_NAME "/vgw_cp"

#define VGW_CP_POLICY_MOD 0
#define VGW_CP_POLICY_RR 1

struct vgw_cp_endpoint {
    uint32_t ip_be;
    uint16_t port; /* host byte order */
    uint16_t _pad;
};

struct vgw_cp_shm {
    uint32_t magic;
    uint32_t version; /* bump last after a full publish */
    uint8_t policy;   /* VGW_CP_POLICY_* */
    uint8_t flags;
    uint16_t count;
    struct vgw_cp_endpoint endpoints[VGW_CP_SHM_MAX_EP];
};

#ifdef __cplusplus
}
#endif

