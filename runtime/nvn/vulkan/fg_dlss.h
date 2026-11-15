// NVIDIA NGX frame generation (fg_dlss.cpp): the FSR 3 path's surface (framegen.h) backed by NGX's
// DLSS-G, behind SAVAGEWIND_FG_DLSS=1 while it is being proven. Optional SDK, outside the repository.
#pragma once
#include "framegen.h"

#ifdef __cplusplus
extern "C" {
#endif

// FrameGeneration.Available on the NGX capability set (RTX 40 series and newer). true only when the
// NGX runtime is up (vk_dlss_init has run) and the feature says it can run here.
bool fg_dlss_available(void);

// The same contract as framegen.h's fg_dispatch: records the interpolated frame between the previous
// call's real frame and this one into f->out. false: failed (logged); the caller falls back.
bool fg_dlss_dispatch(VkCommandBuffer cb, const FgConfig *c, const FgFrame *f);

void fg_dlss_destroy(void);

#ifdef __cplusplus
}
#endif
