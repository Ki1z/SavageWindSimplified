// NVIDIA NGX frame generation (DLSS-G): the FSR 3 path's surface (framegen.h), backed by
// NGX_VK_CREATE_DLSSG / NGX_VK_EVALUATE_DLSSG. The smoke path behind SAVAGEWIND_FG_DLSS=1 (exec.c):
// it passes what FgFrame carries - color, HUD-less, depth, motion vectors with their scale, the
// camera's position and basis and the cut flag, and the optional block's camera matrices and the
// scene's jitter, which refine the interpolation. The feature's own "no interpolated frame" flag
// (dis_init below) suppresses the present, a frame late.
// The SDK itself is optional and remains outside the Savage Wind repository (dlss.cpp's rule).
#include "fg_dlss.h"
#include "dlss.h"  // vk_dlss_available; and vk.h for LOG and g_vk

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32) && DLSS_SDK_PRESENT

#include <nvsdk_ngx_helpers_dlssg_vk.h>
#include <nvsdk_ngx_helpers_vk.h>  // NVSDK_NGX_Create_ImageView_Resource_VK

static bool g_cap_queried, g_cap;
static NVSDK_NGX_Parameter *g_params;
static NVSDK_NGX_Handle *g_feature;
static unsigned g_w, g_h, g_rw, g_rh;
static VkFormat g_fmt;
static bool g_hold_sr;  // (the generation side's size wait: the SR apply skips its rebuilds meanwhile)

bool fg_dlss_holding_sr(void) { return g_hold_sr; }

void fg_dlss_on_title(void) {
    // The feature's life is one gameplay session: the title tears it down, so the SR options can
    // change freely there (nothing pairs with them), and the next entry into the game creates it
    // fresh against whatever SR is now - boot's own sequence, which is the one that survives.
    if (g_feature) {
        if (g_fg_vk.wait_idle) g_fg_vk.wait_idle();
        fg_dlss_destroy();
    }
}

static const char *rname(NVSDK_NGX_Result r) {
    switch (r) {
    case NVSDK_NGX_Result_Success: return "success";
    case NVSDK_NGX_Result_FAIL_FeatureNotSupported: return "feature-not-supported";
    case NVSDK_NGX_Result_FAIL_UnableToInitializeFeature: return "unable-to-initialize-feature";
    case NVSDK_NGX_Result_FAIL_OutOfDate: return "out-of-date";
    case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat: return "unsupported-input-format";
    case NVSDK_NGX_Result_FAIL_MissingInput: return "missing-input";
    case NVSDK_NGX_Result_FAIL_PlatformError: return "platform-error";
    default: return "failure";
    }
}

static bool ok(NVSDK_NGX_Result r, const char *what) {
    if (r == NVSDK_NGX_Result_Success) return true;
    LOG("[dlss-fg] %s: %s (%#x)\n", what, rname(r), (unsigned)r);
    return false;
}

// ---- 4x4 column-major helpers for the camera matrices NGX's optional block takes

static void mat_identity(float *m) { memset(m, 0, 16 * sizeof(float)); m[0] = m[5] = m[10] = m[15] = 1; }

static void mat_mul(float *out, const float *a, const float *b) {  // out = a * b
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int rw = 0; rw < 4; rw++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a[k * 4 + rw] * b[c * 4 + k];
            r[c * 4 + rw] = s;
        }
    memcpy(out, r, sizeof r);
}

static bool mat_invert(float *out, const float *m) {  // the usual adjugate-over-determinant inverse
    float inv[16] = {
        m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
            m[13] * m[6] * m[11] - m[13] * m[7] * m[10],
        -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
            m[13] * m[2] * m[11] + m[13] * m[3] * m[10],
        m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
            m[13] * m[2] * m[7] - m[13] * m[3] * m[6],
        -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
            m[9] * m[2] * m[7] + m[9] * m[3] * m[6],
        -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
            m[12] * m[6] * m[11] + m[12] * m[7] * m[10],
        m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
            m[12] * m[2] * m[11] - m[12] * m[3] * m[10],
        -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[8] * m[2] * m[15] - m[8] * m[3] * m[14] -
            m[12] * m[2] * m[7] + m[12] * m[3] * m[6],
        m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[8] * m[2] * m[11] + m[8] * m[3] * m[10] +
            m[12] * m[2] * m[7] - m[12] * m[3] * m[6],
        m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
            m[12] * m[5] * m[11] - m[12] * m[7] * m[9],
        -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
            m[12] * m[1] * m[11] + m[12] * m[3] * m[9],
        m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[8] * m[1] * m[15] + m[8] * m[3] * m[13] +
            m[12] * m[1] * m[7] - m[12] * m[3] * m[5],
        -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[8] * m[1] * m[11] - m[8] * m[3] * m[9] -
            m[12] * m[1] * m[7] + m[12] * m[3] * m[1],
        -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
            m[12] * m[5] * m[10] + m[12] * m[6] * m[9],
        m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] - m[12] * m[2] * m[9],
        -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[8] * m[1] * m[14] - m[8] * m[2] * m[13] -
            m[12] * m[1] * m[6] + m[12] * m[2] * m[5],
        m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[8] * m[1] * m[10] + m[8] * m[2] * m[9] +
            m[12] * m[1] * m[6] - m[12] * m[2] * m[1]};
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0 || !isfinite(det)) return false;
    float s = 1 / det;
    for (int i = 0; i < 16; i++) out[i] = inv[i] * s;
    return true;
}

// The game's projection from its fovy and planes (the NVN convention: NDC y up, clip z in 0..1),
// no jitter: NGX wants the matrices clean of it (nvsdk_ngx_params_dlssg.h's note).
static void mat_perspective(float *m, float fovy, float aspect, float near_, float far_) {
    mat_identity(m);
    float f = 1 / tanf(fovy * 0.5f);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = far_ / (near_ - far_);
    m[11] = -1;
    m[14] = far_ * near_ / (near_ - far_);
    m[15] = 0;
}

// The runtime's frame-generation images recur every frame (exec.c fg_image's slots), so the views
// behind the NGX resource handles are cached for good and only destroyed with the feature.
static NVSDK_NGX_Resource_VK view_resource(const FgImage *im, bool rw) {
    static struct { VkImage image; VkFormat format; bool rw; VkImageView view; } v[8];
    VkImageView view = NULL;
    for (int i = 0; i < 8; i++) {
        if (v[i].view && v[i].image == im->image && v[i].format == im->format && v[i].rw == rw) {
            view = v[i].view;
            break;
        }
        if (!v[i].view) {
            VkImageViewCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            ci.image = im->image;
            ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ci.format = im->format;
            ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (vkCreateImageView(g_vk.dev, &ci, NULL, &v[i].view) != VK_SUCCESS) break;
            v[i].image = im->image, v[i].format = im->format, v[i].rw = rw;
            view = v[i].view;
            break;
        }
    }
    static const VkComponentMapping id = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                          VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return NVSDK_NGX_Create_ImageView_Resource_VK(view, im->image, range, im->format, im->w, im->h, rw);
}

bool fg_dlss_available(void) {
    if (!g_cap_queried) {
        g_cap_queried = true;
        NVSDK_NGX_Parameter *cap = NULL;
        if (vk_dlss_available() && NVSDK_NGX_VULKAN_GetCapabilityParameters(&cap) == NVSDK_NGX_Result_Success && cap) {
            int a = 0;
            if (NVSDK_NGX_Parameter_GetI(cap, NVSDK_NGX_Parameter_FrameGeneration_Available, &a) == NVSDK_NGX_Result_Success)
                g_cap = a != 0;
        }
        LOG("[dlss-fg] FrameGeneration.Available: %s\n", g_cap ? "yes" : "no");
    }
    return g_cap;
}

void fg_dlss_destroy(void) {
    // (the views in view_resource's cache stay for the device's lifetime: they belong to images that
    //  recur every frame, and a submitted command buffer may still hold their handles)
    if (g_feature) NVSDK_NGX_VULKAN_ReleaseFeature(g_feature);
    g_feature = NULL;
    if (g_params) NVSDK_NGX_VULKAN_DestroyParameters(g_params);
    g_params = NULL;
    g_w = g_h = g_rw = g_rh = 0;
    g_fmt = VK_FORMAT_UNDEFINED;
}

bool fg_dlss_dispatch(VkCommandBuffer cb, const FgConfig *c, const FgFrame *f) {
    if (!cb || !f || !g_vk.dev) return false;
    // This snippet build cannot have its feature remade at a different size, nor survive an SR
    // rebuild next to it carelessly: evaluating the old feature, remaking during a loading stream,
    // and remaking the whole NGX runtime all crash or leave the feature dead (its worker thread
    // reads through a pointer the rebuild invalidated; eight crashes total). A same-size rebuild at
    // a calm gameplay moment is fine (proven mid-gameplay), but a size change has no rebuild to
    // fall back on: the feature is one-size for the process. So: generation pauses while the size
    // differs and resumes with a rebuild when it comes back; an SR rebuild (a preset or upscaler
    // change, DLSS toggled off and on, the loading screens' own size flaps) marks the same
    // excursion, and the rebuild waits for SR to settle and the frame times to calm down first.
    // SAVAGEWIND_FG_DLSS_RESTART_AT=<frame> still forces a same-size rebuild (the experiment).
    static unsigned sr_gen0;      // (the runtime generation at this feature's birth)
    static bool sr_init;
    static bool excursion;       // (the sizes went away since the feature's birth)
    static bool poisoned;        // (the SR feature rebuilt while the sizes were off: the pairing is gone)
    static int restart_at = -1;  // (experiment) rebuild mid-gameplay without a settings change anywhere near
    if (restart_at < 0)
        restart_at = getenv("SAVAGEWIND_FG_DLSS_RESTART_AT") ? atoi(getenv("SAVAGEWIND_FG_DLSS_RESTART_AT")) : 0;
    bool size_changed = g_feature && (g_w != c->display_w || g_h != c->display_h || g_rw != c->render_w ||
                                      g_rh != c->render_h || g_fmt != c->color_format);
    // While this hold is on, the SR side skips its apply (fg_dlss_holding_sr): a loading screen's
    // sizes must not rebuild SR - that generation change during the generation side's own size wait
    // is the one sequence the pair never survives. A change that outlasts the hold (a real preset
    // switch the player keeps) lets SR through after all: it rebuilds, the change is caught below,
    // and generation retires for the session.
    static bool holding;
    static unsigned held;
    holding = size_changed && ++held <= 600;  // (~5-10 s: a loading screen outlasts the hold, a kept
    g_hold_sr = holding;                      //  preset change does not)
    if (size_changed) {
        excursion = true;
        if (sr_init && vk_dlss_settled() && sr_gen0 != vk_dlss_generation()) poisoned = !holding;
        static int said_size;
        if (said_size++ % 600 == 0)
            LOG("[dlss-fg] the image's size is not the feature's (%ux%u): generation pauses until it returns\n",
                g_w, g_h);
        return false;
    }
    held = 0;
    if (poisoned) {
        // A super-resolution preset change does this: its sizes move, and while the generation
        // feature waits them out SR rebuilds too. Remaking the generation feature after that
        // crashed every time it was tried (same-size, settled, calm - all of it), so this pairing
        // retires; the next launch makes both features against the new settings together.
        static int said_poison;
        if (!said_poison++)
            LOG("[dlss-fg] the SR feature rebuilt while the sizes were off: generation stays off until "
                "the next launch\n");
        return false;
    }
    bool settled = vk_dlss_settled();
    // (SR's create retries through quality fallbacks and its input size changes with the scene's:
    //  every such rebuild makes the generation feature stale, so any settled generation change is
    //  an excursion like a size one)
    if (sr_init && settled && sr_gen0 != vk_dlss_generation()) {
        excursion = true;
        sr_gen0 = vk_dlss_generation();  // (the rebuild below is the resync)
    }
    if (!sr_init) {
        sr_gen0 = vk_dlss_generation();
        sr_init = true;
    }
    // The rebuild waits for calm: every crash among these rebuilds happened next to a loading
    // stream - giant frame gaps leave the snippet's worker in its transition state, and releasing
    // or evaluating against that races it. Ninety consecutive sane frames (~1.5 s of gameplay)
    // clear it. (The scene-cut flag is not part of this: the title screen holds it constantly, and
    // it says the history is invalid, not that the runtime is busy.)
    static unsigned quiet;
    quiet = f->frame_ms < 50.0f ? quiet + 1 : 0;
    if (g_feature && excursion && (!settled || quiet < 90)) return false;
    if (g_feature && excursion) {
        LOG("[dlss-fg] rebuilding after the SR feature or the sizes changed\n");
        // (the snippet's cubins hold GPU work: releasing mid-flight crashes inside nvngx_dlssg.dll)
        if (g_fg_vk.wait_idle) g_fg_vk.wait_idle();
        fg_dlss_destroy();
        excursion = false;
    }
    if (g_feature && !settled) return false;  // (SR is mid-rebuild or just took its history-reset
    // frame: the runtime is not whole, and an evaluate beside it races its worker - the 0xf8 reads)
    if (!sr_init) {
        sr_gen0 = vk_dlss_generation();
        sr_init = true;
    }
    if (g_feature && restart_at > 0 && (long long)f->frame_id == restart_at) {
        LOG("[dlss-fg] rebuilding on request (frame %lld)\n", (long long)f->frame_id);
        // (the snippet's cubins hold GPU work: releasing mid-flight crashes inside nvngx_dlssg.dll)
        if (g_fg_vk.wait_idle) g_fg_vk.wait_idle();
        fg_dlss_destroy();
    }
    if (!g_params && !ok(NVSDK_NGX_VULKAN_AllocateParameters(&g_params), "allocate parameters")) return false;
    if (!g_feature) {
        NVSDK_NGX_DLSSG_Create_Params cp = {};
        cp.Width = c->display_w;
        cp.Height = c->display_h;
        cp.NativeBackbufferFormat = (unsigned)c->color_format;
        cp.RenderWidth = c->render_w;
        cp.RenderHeight = c->render_h;
        cp.DynamicResolutionScaling = false;
        NVSDK_NGX_Parameter_SetUI(g_params, NVSDK_NGX_DLSSG_Parameter_Width, cp.Width);  // (the DLSSG ones take
        NVSDK_NGX_Parameter_SetUI(g_params, NVSDK_NGX_DLSSG_Parameter_Height, cp.Height); //  precedence, no warning)
        NVSDK_NGX_Result r = NGX_VK_CREATE_DLSSG(cb, 1, 1, &g_feature, g_params, &cp);
        if (!ok(r, "create feature")) {
            g_feature = NULL;
            return false;
        }
        g_w = c->display_w, g_h = c->display_h, g_rw = c->render_w, g_rh = c->render_h, g_fmt = c->color_format;
        LOG("[dlss-fg] created %ux%u (render %ux%u, color format %d)\n", c->display_w, c->display_h, c->render_w,
            c->render_h, (int)c->color_format);
    }
    NVSDK_NGX_Resource_VK back = view_resource(&f->color, false);
    NVSDK_NGX_Resource_VK depth = view_resource(&f->depth, false);
    NVSDK_NGX_Resource_VK mv = view_resource(&f->motion, false);
    NVSDK_NGX_Resource_VK hudless = view_resource(&f->hudless, false);
    NVSDK_NGX_Resource_VK out = view_resource(&f->out, true);
    // The parameters go on g_params directly and evaluate runs as the generic EvaluateFeature - the
    // set of values a working DLSS-G integration passes, which the Opt_Eval_Params helper's block
    // matches except that it also carries the flag resource NGX rejects with invalid-parameter when
    // one is provided (a NULL leaves the feature's own heuristics to decide).
    float view_clip[16], inv_vp[16], inv_prev[16];
    mat_perspective(view_clip, f->fovy, (float)c->display_w / (float)c->display_h, f->near_, f->far_);
    if (!mat_invert(inv_vp, f->vp)) mat_identity(inv_vp);  // (a bad matrix: identity beats NaNs)
    if (!mat_invert(inv_prev, f->prev_vp)) mat_identity(inv_prev);
    float clip_to_prev[16], prev_to_clip[16], clip_to_view[16];
    mat_mul(clip_to_prev, f->prev_vp, inv_vp);  // this clip -> the previous clip
    mat_mul(prev_to_clip, f->vp, inv_prev);     // the previous clip -> this clip
    if (!mat_invert(clip_to_view, view_clip)) mat_identity(clip_to_view);
    NVSDK_NGX_Parameter *p = g_params;
    NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_DLSSG_Parameter_Backbuffer, &back);
    NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_DLSSG_Parameter_Depth, &depth);
    NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_DLSSG_Parameter_MVecs, &mv);
    NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_DLSSG_Parameter_HUDLess, &hudless);
    NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_DLSSG_Parameter_OutputInterpolated, &out);
    NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_DLSSG_Parameter_UIAlpha, NULL);
    NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_DLSSG_Parameter_OutputReal, NULL);
    NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_DLSSG_Parameter_OutputDisableInterpolation, NULL);
    NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_DLSSG_Parameter_BidirectionalDistortionField, NULL);
    NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_DLSSG_Parameter_CameraViewToClip, view_clip);
    NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_DLSSG_Parameter_ClipToCameraView, clip_to_view);
    NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_DLSSG_Parameter_ClipToPrevClip, clip_to_prev);
    NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_DLSSG_Parameter_PrevClipToClip, prev_to_clip);
    NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_DLSSG_Parameter_MultiFrameCount, 1);   // (2x: one generated
    NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_DLSSG_Parameter_MultiFrameIndex, 1);   //  frame per two real)
    NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_DLSSG_Parameter_Reset, f->reset ? 1 : 0);
    NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_DLSSG_Parameter_DepthInverted, c->depth_inverted ? 1 : 0);
    NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_DLSSG_Parameter_ColorBuffersHDR, 0);  // (the final image is UNORM)
    NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_DLSSG_Parameter_CameraMotionIncluded, 1);  // (the camera's motion is folded in)
    NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_DLSSG_Parameter_MvecDilated, 0);
    NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_DLSSG_Parameter_OrthoProjection, 0);
    NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_DLSSG_Parameter_NotRenderingGameFrames, 0);
    NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_DLSSG_Parameter_MenuDetectionEnabled, 0);
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_DLSSG_Parameter_MvecInvalidValue, -FLT_MAX);
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_DLSSG_Parameter_JitterOffsetX, f->jit_x);  // (the scene's
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_DLSSG_Parameter_JitterOffsetY, f->jit_y);  //  jitter, clip space)
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_DLSSG_Parameter_MvecScaleX, 1);  // (both vector paths' .xy are
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_DLSSG_Parameter_MvecScaleY, 1);  //  in [-1,1] already)
    struct { const char *x, *y, *z; const float *v; } basis[] = {
        {NVSDK_NGX_DLSSG_Parameter_CameraPosX, NVSDK_NGX_DLSSG_Parameter_CameraPosY, NVSDK_NGX_DLSSG_Parameter_CameraPosZ, f->cam_pos},
        {NVSDK_NGX_DLSSG_Parameter_CameraUpX, NVSDK_NGX_DLSSG_Parameter_CameraUpY, NVSDK_NGX_DLSSG_Parameter_CameraUpZ, f->cam_up},
        {NVSDK_NGX_DLSSG_Parameter_CameraRightX, NVSDK_NGX_DLSSG_Parameter_CameraRightY, NVSDK_NGX_DLSSG_Parameter_CameraRightZ, f->cam_right},
        {NVSDK_NGX_DLSSG_Parameter_CameraFwdX, NVSDK_NGX_DLSSG_Parameter_CameraFwdY, NVSDK_NGX_DLSSG_Parameter_CameraFwdZ, f->cam_fwd},
    };
    for (int i = 0; i < 4; i++) {
        NVSDK_NGX_Parameter_SetF(p, basis[i].x, basis[i].v[0]);
        NVSDK_NGX_Parameter_SetF(p, basis[i].y, basis[i].v[1]);
        NVSDK_NGX_Parameter_SetF(p, basis[i].z, basis[i].v[2]);
    }
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_DLSSG_Parameter_CameraNear, f->near_);
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_DLSSG_Parameter_CameraFar, f->far_);
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_DLSSG_Parameter_CameraFOV, f->fovy);
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_DLSSG_Parameter_CameraAspectRatio, (float)c->display_w / (float)c->display_h);
    NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_EvaluateFeature_C(cb, g_feature, p, NULL);  // (the C entry: the
    // C++ one collides with the copy the SR snippet's archive carries)
    if (!ok(r, "evaluate")) {
        static int fails;
        if (fails++ % 60 == 0) {  // (retry from scratch, not every frame: a create is expensive)
            if (g_fg_vk.wait_idle) g_fg_vk.wait_idle();
            fg_dlss_destroy();
        }
        return false;
    }
    static int logged, n;
    if (!logged++)
        LOG("[dlss-fg] first frame: %ux%u (render %ux%u) -> out %ux%u\n", c->display_w, c->display_h, c->render_w,
            c->render_h, f->out.w, f->out.h);
    if (++n % 600 == 0) LOG("[dlss-fg] evaluated %d frames\n", n);
    return true;
}

#else

bool fg_dlss_available(void) { return false; }
bool fg_dlss_dispatch(VkCommandBuffer, const FgConfig *, const FgFrame *) { return false; }
void fg_dlss_destroy(void) {}

#endif
