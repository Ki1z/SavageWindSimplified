// NVIDIA DLSS Super Resolution (Windows): the NVIDIA driver's NGX, loaded at run time, and NVIDIA's DLL next to
// savagewind.exe (dlss.c; ../../dlss_fetch.h downloads it). Elsewhere: unavailable.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "vk.h"

void vk_dlss_required_instance_extensions(const char ***names, unsigned *count);
void vk_dlss_required_device_extensions(const char ***names, unsigned *count);
bool vk_dlss_init(void);  // NGX on g_vk.dev; whether DLSS runs here (false: no DLL here, no NVIDIA driver with NGX)
void vk_dlss_shutdown(void);
bool vk_dlss_available(void);
// Bumped whenever the SR feature is released and made again. Anything sharing the NGX runtime's
// state with it (the frame-generation feature) must treat a change as "my feature is stale".
unsigned vk_dlss_generation(void);
// true once SR has evaluated since its last release: only then may the frame-generation feature
// release or rebuild against the runtime (earlier than that crashes or kills the feature).
bool vk_dlss_settled(void);
// Takes the NGX runtime down and back up (SR's feature included). dlss_wanted backs off while it
// runs, so no frame takes the "NGX can't run here" downgrade. false: there was nothing to restart.
bool vk_dlss_reinitializing(void);
bool vk_dlss_restart(void);
// color at the render size; depth and motion at its size too (MVLowRes): motion RG times mv_scale_x / mv_scale_y is
// each pixel's offset to where it was in the previous frame, in render pixels (exec.c cam_motion's uv differences:
// the motion's size; mvec.c's NDC differences: -w / 2, h / 2; zero, with reset, for none). jitter_x / jitter_y: the
// scene's sub-pixel offset in render pixels. reset: no history (a cut). false: not run (the caller falls back).
bool vk_dlss_apply(VkCommandBuffer cb, TexBackend *color, TexBackend *depth, TexBackend *motion,
                   TexBackend *output, unsigned quality, float jitter_x, float jitter_y, float mv_scale_x,
                   float mv_scale_y, bool reset);

#ifdef __cplusplus
}
#endif
