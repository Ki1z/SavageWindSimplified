// NVIDIA NGX frame generation (DLSS-G): the FSR 3 path's surface (framegen.h), backed by
// NGX_VK_CREATE_DLSSG / NGX_VK_EVALUATE_DLSSG. The smoke path behind SAVAGEWIND_FG_DLSS=1 (exec.c):
// it passes what FgFrame carries - color, HUD-less, depth, motion vectors with their scale, the
// camera's position and basis, the cut flag - and, not yet, the optional camera matrices and the
// scene's jitter, which refine the interpolation but are not required for it to run.
// The SDK itself is optional and remains outside the Savage Wind repository (dlss.cpp's rule).
#include "fg_dlss.h"
#include "dlss.h"  // vk_dlss_available; and vk.h for LOG and g_vk

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
    // The snippet cannot survive its feature being rebuilt while anything else in the NGX runtime is
    // churning (the player's preset change or a loading screen's size flap: SR rebuilds, then a stale
    // or remade FG feature meets freed shared state - read at 0xf8 in its worker thread). What is
    // safe, proven both by the mid-gameplay forced rebuild and the fresh first create: rebuilding the
    // FG feature alone once everything else has been still for a while. So: on any staleness, wait
    // for a calm window, then rebuild; SAVAGEWIND_FG_DLSS_NORECREATE=1 skips generation instead.
    static unsigned sr_gen;      // (the runtime generation this feature was made against)
    static bool sr_valid;
    bool sr_changed = sr_valid && sr_gen != vk_dlss_generation();
    sr_gen = vk_dlss_generation();
    sr_valid = true;
    static FgConfig want = {};   // (a new size settles, the loading screens' tiny renders flap)
    static unsigned settle;      // (calm frames since the last churn: sizes held, SR made no move)
    static bool need_rebuild;
    static int no_recreate = -1;  // (experiment) SAVAGEWIND_FG_DLSS_NORECREATE=1: never rebuild the feature;
    if (no_recreate < 0)          //  once stale, generation stays off for the session (the isolation mode)
        no_recreate = getenv("SAVAGEWIND_FG_DLSS_NORECREATE") != NULL;
    static int reinit = -1;  // (experiment) SAVAGEWIND_FG_DLSS_REINIT=1: take the whole NGX runtime down and
    if (reinit < 0)          //  back up at the rebuild instead of rebuilding the frame-generation feature alone
        reinit = getenv("SAVAGEWIND_FG_DLSS_REINIT") != NULL;
    static int restart_at = -1;  // (experiment) SAVAGEWIND_FG_DLSS_RESTART_AT=<frame>: rebuild mid-gameplay,
    if (restart_at < 0)          //  without a settings change being anywhere near, to test the rebuild itself
        restart_at = getenv("SAVAGEWIND_FG_DLSS_RESTART_AT") ? atoi(getenv("SAVAGEWIND_FG_DLSS_RESTART_AT")) : 0;

    bool size_changed = g_feature && (g_w != c->display_w || g_h != c->display_h || g_rw != c->render_w ||
                                      g_rh != c->render_h || g_fmt != c->color_format);
    if (size_changed || sr_changed) need_rebuild = true;
    bool forced = restart_at > 0 && (long long)f->frame_id == restart_at && g_feature;
    if (need_rebuild && g_feature) {
        if (forced) LOG("[dlss-fg] rebuilding on request (frame %lld)\n", (long long)f->frame_id);
        if (memcmp(&want, c, sizeof *c)) {  // (a size the feature wasn't made for: hold the old one until it holds)
            want = *c;
            settle = 0;
        }
        if (sr_changed) settle = 0;  // (SR moved again: the calm window starts over)
        settle++;
        if (no_recreate && !forced) return false;  // (the isolation mode: stale means off for the session)
        if (!forced && (settle < 10 || memcmp(&want, c, sizeof *c))) return false;  // (not calm yet)
        if (reinit && !forced) {
            fg_dlss_destroy();  // (our handle goes first, then the runtime, then SR and FG are made afresh)
            vk_dlss_restart();
        } else {
            // (the snippet's cubins hold GPU work: releasing mid-flight crashes inside nvngx_dlssg.dll)
            if (g_fg_vk.wait_idle) g_fg_vk.wait_idle();
            fg_dlss_destroy();
        }
        need_rebuild = false;
        settle = 0;
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
    // The optional block's scalars are what makes evaluate accept the frame (MultiFrameCount and
    // friends are only set when it is given), so it is always passed, with the camera's matrices
    // derived from the game's own view-projections where NGX takes them.
    NVSDK_NGX_DLSSG_Opt_Eval_Params opt = {};
    opt.multiFrameCount = 1;  // (2x: one generated frame between two real ones)
    opt.multiFrameIndex = 1;
    float view_clip[16], inv_vp[16], inv_prev[16];
    mat_perspective(view_clip, f->fovy, (float)c->display_w / (float)c->display_h, f->near_, f->far_);
    if (!mat_invert(inv_vp, f->vp)) mat_identity(inv_vp);  // (a bad matrix: identity beats NaNs)
    if (!mat_invert(inv_prev, f->prev_vp)) mat_identity(inv_prev);
    mat_mul((float *)opt.clipToPrevClip, f->prev_vp, inv_vp);  // this clip -> the previous clip
    mat_mul((float *)opt.prevClipToClip, f->vp, inv_prev);     // the previous clip -> this clip
    memcpy(opt.cameraViewToClip, view_clip, sizeof view_clip);
    if (!mat_invert((float *)opt.clipToCameraView, view_clip)) mat_identity((float *)opt.clipToCameraView);
    mat_identity((float *)opt.clipToLensClip);                 // (no lens distortion)
    opt.jitterOffset[0] = opt.jitterOffset[1] = 0;        // (the SR jitter is not plumbed here yet)
    opt.mvecScale[0] = opt.mvecScale[1] = 1;              // (both vector paths' .xy are in [-1,1] already)
    memcpy(opt.cameraPos, f->cam_pos, sizeof opt.cameraPos);
    memcpy(opt.cameraUp, f->cam_up, sizeof opt.cameraUp);
    memcpy(opt.cameraRight, f->cam_right, sizeof opt.cameraRight);
    memcpy(opt.cameraFwd, f->cam_fwd, sizeof opt.cameraFwd);
    opt.cameraNear = f->near_;
    opt.cameraFar = f->far_;
    opt.cameraFOV = f->fovy;
    opt.cameraAspectRatio = (float)c->display_w / (float)c->display_h;
    opt.colorBuffersHDR = false;                          // (the final image is UNORM)
    opt.depthInverted = c->depth_inverted;
    opt.cameraMotionIncluded = true;                      // (both paths fold the camera's motion in)
    opt.reset = f->reset;
    opt.notRenderingGameFrames = false;
    opt.orthoProjection = false;
    opt.motionVectorsDilated = false;
    opt.menuDetectionEnabled = false;
    NVSDK_NGX_VK_DLSSG_Eval_Params ev = {};
    ev.pBackbuffer = &back;
    ev.pDepth = &depth;
    ev.pMVecs = &mv;
    ev.pHudless = &hudless;
    ev.pOutputInterpFrame = &out;
    NVSDK_NGX_Result r = NGX_VK_EVALUATE_DLSSG(cb, g_feature, g_params, &ev, &opt);
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
        LOG("[dlss-fg] first frame: %ux%u (render %ux%u) -> out %ux%u (jitter 0: the SR jitter is not plumbed here "
            "yet)\n",
            c->display_w, c->display_h, c->render_w, c->render_h, f->out.w, f->out.h);
    if (++n % 600 == 0) LOG("[dlss-fg] evaluated %d frames\n", n);
    return true;
}

#else

bool fg_dlss_available(void) { return false; }
bool fg_dlss_dispatch(VkCommandBuffer, const FgConfig *, const FgFrame *) { return false; }
void fg_dlss_destroy(void) {}

#endif
