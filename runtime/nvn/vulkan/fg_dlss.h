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

// The generation side is waiting a size excursion out: the SR apply should skip its rebuilds for
// now (dlss_scene falls back to plain upscaling) so SR's feature never moves under the pair.
bool fg_dlss_holding_sr(void);

// Whether DLSS frame generation arms for the current gameplay session: decided on the title screen
// (live with the setting there, frozen once the game is entered). The rows lock only while this is
// true, and the game's own rows read it to grey the engine choice when it is not.
bool fg_dlss_armed(void);

// Back on the title screen: tear the feature down (its life is one gameplay session), so the SR
// options can change freely there and the next entry into the game remakes it against them.
void fg_dlss_on_title(void);

// The same contract as framegen.h's fg_dispatch: records the interpolated frame between the previous
// call's real frame and this one into f->out. false: failed (logged); the caller falls back.
bool fg_dlss_dispatch(VkCommandBuffer cb, const FgConfig *c, const FgFrame *f);

void fg_dlss_destroy(void);

#ifdef __cplusplus
}
#endif
