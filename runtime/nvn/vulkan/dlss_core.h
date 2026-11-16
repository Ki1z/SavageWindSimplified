// The NGX core's interface, shared between dlss.c (Super Resolution) and fg_dlss.cpp (frame
// generation): dlss.c loads the driver's _nvngx.dll and validates its vtable; everything here is
// implemented there and is safe to call once vk_dlss_core_up() says the core is up. Nothing links
// the NVIDIA SDK: the types mirror what dlss.c declares, and the resource struct is ABI-identical
// to the NVSDK_NGX_Resource_VK image-view layout the core reads.
#pragma once
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DlssNgxParam DlssNgxParam;    // NGX's parameter object (opaque; its vtable is dlss.c's)
typedef struct DlssNgxHandle DlssNgxHandle;  // a created feature

// NVSDK_NGX_Resource_VK as an image view: field for field the layout the core's snippet reads.
typedef struct {
    union {
        struct {
            VkImageView view;
            VkImage image;
            VkImageSubresourceRange range;
            VkFormat format;
            unsigned width, height;
        } img;
        struct { VkBuffer buffer; unsigned bytes; } buf;
    } r;
    int type;  // 0 image view, 1 buffer
    bool rw;
} DlssNgxResourceVK;

// The core is loaded, inited (NVSDK_NGX_VULKAN_Init_Ext2 has run) and its vtable checked. The
// lifetime is the device's: everything below is invalid once vk_dlss_shutdown has run.
bool vk_dlss_core_up(void);

// The capability set (what this driver and GPU run): read FrameGeneration.Available from it.
DlssNgxParam *vk_dlss_capability_params(void);

// A fresh parameter object (NULL: NGX refused). Set and read through the calls below, then pass it
// to vk_dlss_feature_create / vk_dlss_feature_evaluate and free it with vk_dlss_free_params.
DlssNgxParam *vk_dlss_alloc_params(void);
void vk_dlss_free_params(DlssNgxParam *p);

bool vk_dlss_param_set_i(DlssNgxParam *p, const char *name, int v);
bool vk_dlss_param_set_u(DlssNgxParam *p, const char *name, unsigned v);
bool vk_dlss_param_set_f(DlssNgxParam *p, const char *name, float v);
bool vk_dlss_param_set_p(DlssNgxParam *p, const char *name, void *v);
bool vk_dlss_param_get_i(const DlssNgxParam *p, const char *name, int *v);
bool vk_dlss_param_get_u(const DlssNgxParam *p, const char *name, unsigned *v);
bool vk_dlss_param_get_f(const DlssNgxParam *p, const char *name, float *v);
bool vk_dlss_param_get_p(const DlssNgxParam *p, const char *name, void **v);

// NGX's feature calls. create and evaluate record into cb; false: NGX refused (logged inside).
bool vk_dlss_feature_create(VkCommandBuffer cb, int feature_id, DlssNgxParam *p, DlssNgxHandle **out);
bool vk_dlss_feature_evaluate(VkCommandBuffer cb, const DlssNgxHandle *h, DlssNgxParam *p);
void vk_dlss_feature_release(DlssNgxHandle *h);

// The Super Resolution feature's identity: it changes whenever SR's feature is (re)made - a size,
// format or quality change. The generation side compares it against the value it recorded when its
// own feature was made: a difference means the pair's halves were built against different SR.
unsigned vk_dlss_generation(void);

// false while SR is mid-rebuild or just ran a history-reset frame: the runtime is not whole, and an
// evaluate beside it races its worker.
bool vk_dlss_settled(void);

#ifdef __cplusplus
}
#endif
