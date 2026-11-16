// NVIDIA NGX frame generation (DLSS-G) through the NGX core that dlss.c loaded: the FSR 3 path's
// surface (framegen.h), feature id 11, with the parameter names of NVIDIA's documented interface
// hand-declared from the SDK's headers (no NVIDIA SDK code, header or library is compiled in or
// linked - dlss.c's rule). It passes what FgFrame carries - color, HUD-less, depth, motion vectors
// with their scale, the camera's position and basis and the cut flag, and the optional block's
// camera matrices and the scene's jitter, which refine the interpolation. The feature's own "no
// interpolated frame" flag (OutputDisableInterpolation) is left unset: NULL lets the feature's
// heuristics decide.
//
// Threading: NGX records into cb and may wait for the device, and dlss.c runs SR's own calls on
// the recorder thread - so the generation side's create and evaluate go there too (rec_custom,
// like FSR 3's fg_run): the two NGX features never run on different threads, which the in-place
// rebuild experiments showed they must not. The dispatch itself (the session's bookkeeping: the
// size hold, the SR-generation watch, the calm-frame wait) stays on the queue thread; whether the
// recorded frame worked is known a frame late (g_fg_failed), as with FSR 3.
#include "fg_dlss.h"
#include "dlss.h"

extern "C" {
#include "../../settings.h"
#include "../../framepace.h"
#include "dlss_core.h"  // the loaded NGX core (dlss.c): SR's runtime, shared with the generation side
#include "record.h"     // rec_custom, rec_on: the calls run on the recorder thread, in their turn
}

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>

#ifdef _WIN32

// ---- NGX's names for the generation feature (the SDK's nvsdk_ngx_defs*.h, DLSS 310's strings)
enum { NGX_FEATURE_FRAME_GENERATION = 11 };  // NVSDK_NGX_Feature_FrameGeneration
static const char
    *P_CreationNodeMask = "CreationNodeMask", *P_VisibilityNodeMask = "VisibilityNodeMask",
    *P_Width = "Width", *P_Height = "Height",
    *P_DLSSG_Width = "DLSSG.Width", *P_DLSSG_Height = "DLSSG.Height",  // (take precedence, no warning)
    *P_BackbufferFormat = "DLSSG.BackbufferFormat",
    *P_Backbuffer = "DLSSG.Backbuffer", *P_MVecs = "DLSSG.MVecs", *P_Depth = "DLSSG.Depth",
    *P_HUDLess = "DLSSG.HUDLess", *P_OutputInterpolated = "DLSSG.OutputInterpolated",
    *P_OutputReal = "DLSSG.OutputReal", *P_UIAlpha = "DLSSG.UIAlpha",
    *P_OutputDisableInterpolation = "DLSSG.OutputDisableInterpolation",
    *P_BidirectionalDistortionField = "DLSSG.BidirectionalDistortionField",
    *P_CameraViewToClip = "DLSSG.CameraViewToClip", *P_ClipToCameraView = "DLSSG.ClipToCameraView",
    *P_ClipToPrevClip = "DLSSG.ClipToPrevClip", *P_PrevClipToClip = "DLSSG.PrevClipToClip",
    *P_MultiFrameCount = "DLSSG.MultiFrameCount", *P_MultiFrameIndex = "DLSSG.MultiFrameIndex",
    *P_Reset = "DLSSG.Reset", *P_DepthInverted = "DLSSG.DepthInverted",
    *P_ColorBuffersHDR = "DLSSG.ColorBuffersHDR", *P_CameraMotionIncluded = "DLSSG.CameraMotionIncluded",
    *P_MvecDilated = "DLSSG.MvecDilated", *P_OrthoProjection = "DLSSG.OrthoProjection",
    *P_NotRenderingGameFrames = "DLSSG.NotRenderingGameFrames",
    *P_MenuDetectionEnabled = "DLSSG.MenuDetectionEnabled", *P_MvecInvalidValue = "DLSSG.MvecInvalidValue",
    *P_JitterOffsetX = "DLSSG.JitterOffsetX", *P_JitterOffsetY = "DLSSG.JitterOffsetY",
    *P_MvecScaleX = "DLSSG.MvecScaleX", *P_MvecScaleY = "DLSSG.MvecScaleY",
    *P_CameraPosX = "DLSSG.CameraPosX", *P_CameraPosY = "DLSSG.CameraPosY", *P_CameraPosZ = "DLSSG.CameraPosZ",
    *P_CameraUpX = "DLSSG.CameraUpX", *P_CameraUpY = "DLSSG.CameraUpY", *P_CameraUpZ = "DLSSG.CameraUpZ",
    *P_CameraRightX = "DLSSG.CameraRightX", *P_CameraRightY = "DLSSG.CameraRightY",
    *P_CameraRightZ = "DLSSG.CameraRightZ", *P_CameraFwdX = "DLSSG.CameraFwdX",
    *P_CameraFwdY = "DLSSG.CameraFwdY", *P_CameraFwdZ = "DLSSG.CameraFwdZ",
    *P_CameraNear = "DLSSG.CameraNear", *P_CameraFar = "DLSSG.CameraFar", *P_CameraFOV = "DLSSG.CameraFOV",
    *P_CameraAspectRatio = "DLSSG.CameraAspectRatio",
    *P_FrameGeneration_Available = "FrameGeneration.Available";

static bool g_cap_queried, g_cap;
static DlssNgxParam *g_params;
static DlssNgxHandle *g_feature;
static unsigned g_w, g_h, g_rw, g_rh;
static VkFormat g_fmt;
static std::atomic_int g_fg_failed;  // (a recorded frame failed: known to the queue thread a frame late)
static bool g_rebuild_wanted;        // (an evaluate failed mid-flight: tear the feature down, calmly)
static bool g_hold_sr;  // (the generation side's size wait: the SR apply skips its rebuilds meanwhile)
// one gameplay session's bookkeeping (fg_dlss_on_title resets them with the feature)
static unsigned g_sess_sr_gen0;   // (the runtime generation at this feature's birth)
static bool g_sess_sr_init;
static bool g_sess_excursion;     // (the sizes went away since the feature's birth)
static bool g_sess_poisoned;      // (the SR feature rebuilt while the sizes were off: the pairing is gone)
static bool g_sess_holding;       // (the size wait is young: SR holds off)
static unsigned g_sess_held;

bool fg_dlss_holding_sr(void) { return g_hold_sr; }

bool fg_dlss_armed(void) {
    // The engine is decided when a gameplay session starts: on the title screen this follows the
    // setting live, and stepping into the game freezes it until the next title visit - so toggling
    // DLSS mid-game records the choice without arming the feature (the settings stay unlocked, only
    // the choice waits, per the row's hint).
    static int armed = -1;
    if (armed < 0 || framepace_title_ui())
        armed = g_settings.frame_gen && g_settings.frame_gen_dlss && fg_dlss_available();
    return armed != 0;
}

void fg_dlss_on_title(void) {
    // The feature's life is one gameplay session: the title tears it down, so the SR options can
    // change freely there (nothing pairs with them), and the next entry into the game creates it
    // fresh against whatever SR is now - boot's own sequence, which is the one that survives. The
    // session's bookkeeping resets with it: a retire earned mid-game (an SR rebuild during the
    // size wait - a window resize or a kept preset change) does not follow the player here.
    if (g_feature) {
        if (g_fg_vk.wait_idle) g_fg_vk.wait_idle();
        fg_dlss_destroy();
    }
    g_sess_sr_gen0 = 0;
    g_sess_sr_init = false;
    g_sess_excursion = false;
    g_sess_poisoned = false;
    g_sess_holding = false;
    g_sess_held = 0;
    g_rebuild_wanted = false;
    atomic_store(&g_fg_failed, 0);
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
// no jitter: NGX wants the matrices clean of it (the SDK's nvsdk_ngx_params_dlssg.h note).
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
// behind the NGX resource handles are cached for good and only destroyed with the device.
static DlssNgxResourceVK view_resource(const FgImage *im, bool rw) {
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
    DlssNgxResourceVK r;
    memset(&r, 0, sizeof r);
    r.r.img.view = view;
    r.r.img.image = im->image;
    r.r.img.range = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    r.r.img.format = im->format;
    r.r.img.width = im->w, r.r.img.height = im->h;
    r.type = 0, r.rw = rw;
    return r;
}

bool fg_dlss_available(void) {
    if (!g_cap_queried) {
        g_cap_queried = true;
        DlssNgxParam *cap = vk_dlss_core_up() ? vk_dlss_capability_params() : NULL;
        int a = 0;
        if (cap && vk_dlss_param_get_i(cap, P_FrameGeneration_Available, &a)) g_cap = a != 0;
        LOG("[dlss-fg] FrameGeneration.Available: %s\n", g_cap ? "yes" : "no");
    }
    return g_cap;
}

void fg_dlss_destroy(void) {
    // (the views in view_resource's cache stay for the device's lifetime: they belong to images that
    //  recur every frame, and a submitted command buffer may still hold their handles)
    if (g_feature) vk_dlss_feature_release(g_feature);
    g_feature = NULL;
    if (g_params) vk_dlss_free_params(g_params);
    g_params = NULL;
    g_w = g_h = g_rw = g_rh = 0;
    g_fmt = VK_FORMAT_UNDEFINED;
}

// ---- the recorded half: on the recorder thread, in its turn among dlss.c's SR calls

// One frame's work, filled on the queue thread (the views' cache and the matrices), executed on the
// recorder thread (the NGX calls: create when due, then evaluate).
typedef struct {
    VkCommandBuffer cb;
    DlssNgxResourceVK back, depth, mv, hudless, out;
    unsigned disp_w, disp_h, rend_w, rend_h;  // (the create block, when the feature is due)
    VkFormat color_format;
    bool reset, depth_inverted;
    float jit_x, jit_y, near_, far_, fovy, aspect;
    float cam_pos[3], cam_up[3], cam_right[3], cam_fwd[3];
    float view_clip[16], clip_to_view[16], clip_to_prev[16], prev_to_clip[16];
} FgDlssJob;

static void fg_dlss_run(void *arg) {
    FgDlssJob *j = (FgDlssJob *)arg;
    if (!vk_dlss_core_up()) { atomic_store(&g_fg_failed, 1); return; }
    if (!g_params && !(g_params = vk_dlss_alloc_params())) { atomic_store(&g_fg_failed, 1); return; }
    if (!g_feature) {
        DlssNgxParam *p = g_params;
        vk_dlss_param_set_u(p, P_CreationNodeMask, 1);
        vk_dlss_param_set_u(p, P_VisibilityNodeMask, 1);
        vk_dlss_param_set_u(p, P_Width, j->disp_w);
        vk_dlss_param_set_u(p, P_Height, j->disp_h);
        vk_dlss_param_set_u(p, P_DLSSG_Width, j->disp_w);
        vk_dlss_param_set_u(p, P_DLSSG_Height, j->disp_h);
        vk_dlss_param_set_u(p, P_BackbufferFormat, (unsigned)j->color_format);
        if (!vk_dlss_feature_create(j->cb, NGX_FEATURE_FRAME_GENERATION, p, &g_feature)) {
            g_feature = NULL;
            atomic_store(&g_fg_failed, 1);
            return;
        }
        g_w = j->disp_w, g_h = j->disp_h, g_rw = j->rend_w, g_rh = j->rend_h, g_fmt = j->color_format;
        static int logged;
        if (!logged++)
            LOG("[dlss-fg] created %ux%u (render %ux%u, color format %d)\n", j->disp_w, j->disp_h, j->rend_w,
                j->rend_h, (int)j->color_format);
    }
    DlssNgxParam *p = g_params;
    vk_dlss_param_set_p(p, P_Backbuffer, &j->back);
    vk_dlss_param_set_p(p, P_Depth, &j->depth);
    vk_dlss_param_set_p(p, P_MVecs, &j->mv);
    vk_dlss_param_set_p(p, P_HUDLess, &j->hudless);
    vk_dlss_param_set_p(p, P_OutputInterpolated, &j->out);
    vk_dlss_param_set_p(p, P_UIAlpha, NULL);
    vk_dlss_param_set_p(p, P_OutputReal, NULL);
    vk_dlss_param_set_p(p, P_OutputDisableInterpolation, NULL);
    vk_dlss_param_set_p(p, P_BidirectionalDistortionField, NULL);
    vk_dlss_param_set_p(p, P_CameraViewToClip, j->view_clip);
    vk_dlss_param_set_p(p, P_ClipToCameraView, j->clip_to_view);
    vk_dlss_param_set_p(p, P_ClipToPrevClip, j->clip_to_prev);
    vk_dlss_param_set_p(p, P_PrevClipToClip, j->prev_to_clip);
    vk_dlss_param_set_u(p, P_MultiFrameCount, 1);  // (2x: one generated
    vk_dlss_param_set_u(p, P_MultiFrameIndex, 1);  //  frame per two real)
    vk_dlss_param_set_u(p, P_Reset, j->reset ? 1 : 0);
    vk_dlss_param_set_i(p, P_DepthInverted, j->depth_inverted ? 1 : 0);  // (exec.c's depth convention)
    vk_dlss_param_set_u(p, P_ColorBuffersHDR, 0);  // (the final image is UNORM)
    vk_dlss_param_set_u(p, P_CameraMotionIncluded, 1);  // (the camera's motion is folded in)
    vk_dlss_param_set_u(p, P_MvecDilated, 0);
    vk_dlss_param_set_u(p, P_OrthoProjection, 0);
    vk_dlss_param_set_u(p, P_NotRenderingGameFrames, 0);
    vk_dlss_param_set_u(p, P_MenuDetectionEnabled, 0);
    vk_dlss_param_set_f(p, P_MvecInvalidValue, -FLT_MAX);
    vk_dlss_param_set_f(p, P_JitterOffsetX, j->jit_x);  // (the scene's
    vk_dlss_param_set_f(p, P_JitterOffsetY, j->jit_y);  //  jitter, clip space)
    vk_dlss_param_set_f(p, P_MvecScaleX, 1);  // (both vector paths' .xy are
    vk_dlss_param_set_f(p, P_MvecScaleY, 1);  //  in [-1,1] already)
    struct { const char *x, *y, *z; const float *v; } basis[] = {
        {P_CameraPosX, P_CameraPosY, P_CameraPosZ, j->cam_pos},
        {P_CameraUpX, P_CameraUpY, P_CameraUpZ, j->cam_up},
        {P_CameraRightX, P_CameraRightY, P_CameraRightZ, j->cam_right},
        {P_CameraFwdX, P_CameraFwdY, P_CameraFwdZ, j->cam_fwd},
    };
    for (int i = 0; i < 4; i++) {
        vk_dlss_param_set_f(p, basis[i].x, basis[i].v[0]);
        vk_dlss_param_set_f(p, basis[i].y, basis[i].v[1]);
        vk_dlss_param_set_f(p, basis[i].z, basis[i].v[2]);
    }
    vk_dlss_param_set_f(p, P_CameraNear, j->near_);
    vk_dlss_param_set_f(p, P_CameraFar, j->far_);
    vk_dlss_param_set_f(p, P_CameraFOV, j->fovy);
    vk_dlss_param_set_f(p, P_CameraAspectRatio, j->aspect);
    if (!vk_dlss_feature_evaluate(j->cb, g_feature, p)) {
        atomic_store(&g_fg_failed, 1);  // (the queue thread tears the feature down: wait_idle first)
        g_rebuild_wanted = true;
        return;
    }
    static int n;
    if (++n % 600 == 0) LOG("[dlss-fg] evaluated %d frames\n", n);
}

bool fg_dlss_dispatch(VkCommandBuffer cb, const FgConfig *c, const FgFrame *f) {
    if (!cb || !f || !g_vk.dev) return false;
    // The recorder's outcome for the frame before this one. A failed evaluate is retried from
    // scratch, not every frame: a create is expensive, and releasing mid-flight needs the device
    // idle first (the snippet's cubins hold GPU work).
    if (atomic_exchange(&g_fg_failed, 0)) {
        static int fails;
        if (fails++ % 60 == 0) {
            if (g_fg_vk.wait_idle) g_fg_vk.wait_idle();
            fg_dlss_destroy();
        }
        return false;
    }
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
    static int restart_at = -1;  // (experiment) rebuild mid-gameplay without a settings change anywhere near
    if (restart_at < 0)
        restart_at = getenv("SAVAGEWIND_FG_DLSS_RESTART_AT") ? atoi(getenv("SAVAGEWIND_FG_DLSS_RESTART_AT")) : 0;
    bool size_changed = g_feature && g_w && (g_w != c->display_w || g_h != c->display_h || g_rw != c->render_w ||
                                      g_rh != c->render_h || g_fmt != c->color_format);  // (g_w 0: the create is
    // still in the recorder's queue - the frame after the first one sees the real sizes)
    // While this hold is on, the SR side skips its apply (fg_dlss_holding_sr): a loading screen's
    // sizes must not rebuild SR - that generation change during the generation side's own size wait
    // is the one sequence the pair never survives. A change that outlasts the hold (a real preset
    // switch the player keeps, a window resize) lets SR through after all: it rebuilds, the change
    // is caught below, and generation retires until the title screen resets the session.
    bool holding = size_changed && ++g_sess_held <= 600;  // (~5-10 s: a loading screen outlasts the
    g_hold_sr = g_sess_holding = holding;                 //  hold, a kept preset change does not)
    if (size_changed) {
        g_sess_excursion = true;
        if (g_sess_sr_init && vk_dlss_settled() && g_sess_sr_gen0 != vk_dlss_generation()) g_sess_poisoned = !holding;
        if (!holding)
            g_sess_poisoned = true;  // (the sizes are really staying: see the poisoned branch below)
        static int said_size;
        if (said_size++ % 600 == 0)
            LOG("[dlss-fg] the image's size is not the feature's (%ux%u): generation pauses until it returns\n",
                g_w, g_h);
        return false;
    }
    g_sess_held = 0;
    if (g_sess_poisoned) {
        // A render size that really moved (a kept preset, a resized window) or an SR rebuild that
        // slipped past the hold - a display-mode switch re-inits the render targets on a path that
        // never passes through the scene apply - leaves the pair built against different sizes.
        // Remaking the generation feature beside the rebuilt SR crashed every time it was tried in
        // play, however calm; the fresh-create windows that survive are boot and the title screen.
        // So the pairing retires until the player is back on the title, which tears the feature
        // down and resets this session; the next entry into the game makes both against the new
        // sizes together.
        static int said_poison;
        if (!said_poison++)
            LOG("[dlss-fg] the render size moved: generation stays off until the next title screen\n");
        return false;
    }
    bool settled = vk_dlss_settled();
    // (SR's create retries through quality fallbacks and its input size changes with the scene's:
    //  every such rebuild makes the generation feature stale, so any settled generation change is
    //  an excursion like a size one)
    if (g_sess_sr_init && settled && g_sess_sr_gen0 != vk_dlss_generation()) {
        g_sess_excursion = true;
        g_sess_sr_gen0 = vk_dlss_generation();  // (the rebuild below is the resync)
    }
    if (!g_sess_sr_init) {
        g_sess_sr_gen0 = vk_dlss_generation();
        g_sess_sr_init = true;
    }
    // The rebuild waits for calm: every crash among these rebuilds happened next to a loading
    // stream - giant frame gaps leave the snippet's worker in its transition state, and releasing
    // or evaluating against that races it. Ninety consecutive sane frames (~1.5 s of gameplay)
    // clear it. (The scene-cut flag is not part of this: the title screen holds it constantly, and
    // it says the history is invalid, not that the runtime is busy.)
    static unsigned quiet;
    quiet = f->frame_ms < 50.0f ? quiet + 1 : 0;
    if (g_feature && g_sess_excursion && (!settled || quiet < 90)) return false;
    if (g_feature && g_sess_excursion) {
        LOG("[dlss-fg] rebuilding after the SR feature or the sizes changed\n");
        // (the snippet's cubins hold GPU work: releasing mid-flight crashes inside nvngx_dlssg.dll)
        if (g_fg_vk.wait_idle) g_fg_vk.wait_idle();
        fg_dlss_destroy();
        g_sess_excursion = false;
    }
    if (g_feature && g_rebuild_wanted) {
        // (an evaluate failed last frame: the feature's own worker may still hold it - the same
        //  wait, then out with it, and the next dispatch creates it again)
        if (g_fg_vk.wait_idle) g_fg_vk.wait_idle();
        fg_dlss_destroy();
        g_rebuild_wanted = false;
        return false;
    }
    if (g_feature && !settled) return false;  // (SR is mid-rebuild or just took its history-reset
    // frame: the runtime is not whole, and an evaluate beside it races its worker - the 0xf8 reads)
    if (g_feature && restart_at > 0 && (long long)f->frame_id == restart_at) {
        LOG("[dlss-fg] rebuilding on request (frame %lld)\n", (long long)f->frame_id);
        // (the snippet's cubins hold GPU work: releasing mid-flight crashes inside nvngx_dlssg.dll)
        if (g_fg_vk.wait_idle) g_fg_vk.wait_idle();
        fg_dlss_destroy();
    }
    // The matrices for this frame (the queue thread: they read only FgFrame and FgConfig).
    float view_clip[16], inv_vp[16], inv_prev[16];
    mat_perspective(view_clip, f->fovy, (float)c->display_w / (float)c->display_h, f->near_, f->far_);
    if (!mat_invert(inv_vp, f->vp)) mat_identity(inv_vp);  // (a bad matrix: identity beats NaNs)
    if (!mat_invert(inv_prev, f->prev_vp)) mat_identity(inv_prev);
    float clip_to_prev[16], prev_to_clip[16], clip_to_view[16];
    mat_mul(clip_to_prev, f->prev_vp, inv_vp);  // this clip -> the previous clip
    mat_mul(prev_to_clip, f->vp, inv_prev);     // the previous clip -> this clip
    if (!mat_invert(clip_to_view, view_clip)) mat_identity(clip_to_view);
    // The recorded job: everything the NGX calls read, copied below (the images recur, but the
    // frame's views and scalars are this frame's).
    static FgDlssJob jobs[8];  // (the recorder is never 8 frames behind: a submission a frame waits for it)
    static unsigned nj;
    FgDlssJob *j = &jobs[nj++ & 7];
    memset(j, 0, sizeof *j);
    j->cb = cb;
    j->back = view_resource(&f->color, false);
    j->depth = view_resource(&f->depth, false);
    j->mv = view_resource(&f->motion, false);
    j->hudless = view_resource(&f->hudless, false);
    j->out = view_resource(&f->out, true);
    j->disp_w = c->display_w, j->disp_h = c->display_h, j->rend_w = c->render_w, j->rend_h = c->render_h;
    j->color_format = c->color_format;
    j->reset = f->reset, j->depth_inverted = c->depth_inverted;
    j->jit_x = f->jit_x, j->jit_y = f->jit_y;
    j->near_ = f->near_, j->far_ = f->far_, j->fovy = f->fovy;
    j->aspect = (float)c->display_w / (float)c->display_h;
    memcpy(j->cam_pos, f->cam_pos, sizeof j->cam_pos);
    memcpy(j->cam_up, f->cam_up, sizeof j->cam_up);
    memcpy(j->cam_right, f->cam_right, sizeof j->cam_right);
    memcpy(j->cam_fwd, f->cam_fwd, sizeof j->cam_fwd);
    memcpy(j->view_clip, view_clip, sizeof view_clip);
    memcpy(j->clip_to_view, clip_to_view, sizeof clip_to_view);
    memcpy(j->clip_to_prev, clip_to_prev, sizeof clip_to_prev);
    memcpy(j->prev_to_clip, prev_to_clip, sizeof prev_to_clip);
    if (rec_on()) rec_custom(fg_dlss_run, j);
    else fg_dlss_run(j);
    static int logged;
    if (!logged++)
        LOG("[dlss-fg] first frame: %ux%u (render %ux%u) -> out %ux%u\n", c->display_w, c->display_h, c->render_w,
            c->render_h, f->out.w, f->out.h);
    return true;
}

#else

bool fg_dlss_available(void) { return false; }
bool fg_dlss_holding_sr(void) { return false; }
bool fg_dlss_armed(void) { return false; }
void fg_dlss_on_title(void) {}
bool fg_dlss_dispatch(VkCommandBuffer, const FgConfig *, const FgFrame *) { return false; }
void fg_dlss_destroy(void) {}

#endif
