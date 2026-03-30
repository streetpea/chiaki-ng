#pragma once

#include "streamsession.h"
#include "settings.h"

#include <QMutex>
#include <QAtomicInteger>
#include <QWindow>
#include <QQuickWindow>
#include <QLoggingCategory>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_vulkan.h>
#include <libplacebo/opengl.h>
#include <libplacebo/options.h>
#include <libplacebo/vulkan.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/utils/frame_queue.h>
#include <libplacebo/log.h>
#include <libplacebo/cache.h>
}

#include <vulkan/vulkan.h>
#if defined(Q_OS_LINUX)
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
#include <vulkan/vulkan_wayland.h>
#elif defined(Q_OS_MACOS)
#include <vulkan/vulkan_metal.h>
#elif defined(Q_OS_WIN)
#include <vulkan/vulkan_win32.h>
#endif

Q_DECLARE_LOGGING_CATEGORY(chiakiGui);

class Settings;
class StreamSession;
class QmlBackend;
class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;

class QmlMainWindow : public QWindow
{
    Q_OBJECT
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY droppedFramesChanged)
    Q_PROPERTY(bool keepVideo READ keepVideo WRITE setKeepVideo NOTIFY keepVideoChanged)
    Q_PROPERTY(VideoMode videoMode READ videoMode WRITE setVideoMode NOTIFY videoModeChanged)
    Q_PROPERTY(float ZoomFactor READ zoomFactor WRITE setZoomFactor NOTIFY zoomFactorChanged)
    Q_PROPERTY(VideoPreset videoPreset READ videoPreset WRITE setVideoPreset NOTIFY videoPresetChanged)
    Q_PROPERTY(bool directStream READ directStream NOTIFY directStreamChanged)
    Q_PROPERTY(int runtimeRendererBackend READ runtimeRendererBackend CONSTANT)
    Q_PROPERTY(double queueDepthAverage READ queueDepthAverage NOTIFY queueDepthAverageChanged)
    Q_PROPERTY(double pendingFrameAge READ pendingFrameAge NOTIFY pendingFrameAgeChanged)

public:
    enum class VideoMode {
        Normal,
        Stretch,
        Zoom
    };
    Q_ENUM(VideoMode);

    enum class VideoPreset {
        Fast,
        Default,
        HighQuality,
        HighQualitySpatial,
        HighQualityAdvancedSpatial,
        Custom
    };
    Q_ENUM(VideoPreset);

    QmlMainWindow(Settings *settings,  bool exit_app_on_stream_exit = false);
    QmlMainWindow(const StreamSessionConnectInfo &connect_info);
    ~QmlMainWindow();
    void updateWindowType(WindowType type);
    void setSettings(Settings *new_settings);

    bool hasVideo() const;
    int droppedFrames() const;
    void increaseDroppedFrames();

    bool directStream() const;
    int runtimeRendererBackend() const { return static_cast<int>(render_backend); }

    bool keepVideo() const;
    void setKeepVideo(bool keep);

    VideoMode videoMode() const;
    void setVideoMode(VideoMode mode);

    float zoomFactor() const;
    void setZoomFactor(float factor);

    double queueDepthAverage() const;
    double pendingFrameAge() const;

    bool amdCard() const;
    bool nvidiaCard() const;
    bool wasMaximized() const { return was_maximized; };
    bool isWindowAdjustable() const { return is_window_adjustable; }
    void setWindowAdjustable(bool adjustable) { is_window_adjustable = adjustable; }

    void fullscreenTime();
    void normalTime();

    bool isStreamWindowAdjustable() { return is_stream_window_adjustable; }
    void setStreamWindowAdjustable(bool adjustable) { is_stream_window_adjustable = adjustable; }

    QmlBackend *getBackend();

    VideoPreset videoPreset() const;
    void setVideoPreset(VideoPreset mode);

    Q_INVOKABLE void grabInput();
    Q_INVOKABLE void releaseInput();

public slots:
    void resetPlaceboQueue();
    void schedulePlaceboReset();

    void updatePlacebo();
    void updateVSync();
    void show();
    void presentFrame(ChiakiFfmpegFrame frame, int32_t frames_lost);

    AVBufferRef *vulkanHwDeviceCtx();

signals:
    void hasVideoChanged();
    void droppedFramesChanged();
    void keepVideoChanged();
    void videoModeChanged();
    void zoomFactorChanged();
    void videoPresetChanged();
    void menuRequested();
    void directStreamChanged();
    void queueDepthAverageChanged();
    void pendingFrameAgeChanged();

private:
    bool makeOpenGLContextCurrent();
    void doneOpenGLContextCurrent();

    void init(Settings *settings, bool exit_app_on_stream_exit = false);
    pl_gpu placeboGpu() const;
    void update();
    void scheduleUpdate();
    void createSwapchain();
    void destroySwapchain();
    void resizeSwapchain();
    void updateSwapchain();
    void sync();
    void beginFrame();
    void endFrame();
    void render();
    void applyPendingFrame();
    bool applyPendingFrameIfQueueHasCapacity();
    bool hasPendingFrame();
    void storePendingFrame(ChiakiFfmpegFrame &frame);
    void refreshPendingFrameAge();
    bool handleShortcut(QKeyEvent *event);
    bool event(QEvent *event) override;
    QObject *focusObject() const override;
    void updateQueueDepthAverage(int depth);
    void updatePendingFrameAge(double age);
    void snapshotLastFrame(AVFrame *frame, double pts, float duration);
    void clearSnapshotFrame();
    void clearFallbackFrame();
    void invalidateHeldFrame();
    bool ensureHeldFrameTarget(const struct pl_frame &target_frame);
    bool updateHeldFrame(const struct pl_frame_mix &frame_mix,
                         const struct pl_render_params &params,
                         const struct pl_frame &target_frame);
    bool renderHeldFrame(const struct pl_frame &target_frame,
                         const struct pl_render_params &params);
    bool enqueueKeptFrame(double queue_pts_origin_hint, bool deinterlace_enabled, double &used_origin);
    bool has_video = false;
    struct pl_queue_params qparams;
    struct pl_frame_mix frame_mix;
    uint64_t ts_start = 0;
    double queue_pts_origin = -1.0;
    bool playback_started = false;
    bool was_maximized = false;
    bool amd_card = false;
    bool nvidia_card = false;
    bool direct_stream = false;
    bool keep_video = false;
    RenderBackend render_backend = RenderBackend::Vulkan;
    int grab_input = 0;
    int dropped_frames = 0;
    bool is_window_adjustable = false;
    bool is_stream_window_adjustable = false;
    QAtomicInteger<int> dropped_frames_current = 0;
    bool going_full = false;
    VideoMode video_mode = VideoMode::Normal;
    float zoom_factor = 0;
    VideoPreset video_preset = VideoPreset::HighQuality;
    Settings *settings = {};

    QmlBackend *backend = {};
    StreamSession *session = {};
    AVBufferRef *vulkan_hw_dev_ctx = nullptr;
    double queue_depth_average = 0.0;
    double pending_frame_age = 0.0;
    uint64_t last_placebo_reset_ts = 0;
    mutable QMutex pending_frame_age_mutex;

    pl_cache placebo_cache = {};
    pl_log placebo_log = {};
    pl_vk_inst placebo_vk_inst = {};
    pl_vulkan placebo_vulkan = {};
    pl_opengl placebo_opengl = {};
    pl_swapchain placebo_swapchain = {};
    pl_renderer placebo_renderer = {};
    pl_queue placebo_queue = {};
    std::array<pl_tex, 8> placebo_tex{};
    pl_tex held_frame_tex = {};
    struct pl_frame held_frame = {};
    pl_rect2df held_frame_crop = {};
    bool held_frame_crop_valid = false;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    int vk_decode_queue_index = -1;
    QSize swapchain_size;
    QThread *render_thread = {};
    bool owns_render_thread = false;
    QMutex render_schedule_mutex;
    QMutex placebo_state_mutex;
    bool render_scheduled = false;
    bool render_pending = false;
    QMutex pending_frame_mutex;
    AVFrame *pending_frame = nullptr;
    double pending_pts = 0.0;
    float pending_duration = 0.0f;
    double pending_frame_queue_origin = 0.0;
    std::atomic<uint64_t> snapshot_generation{0};
    std::atomic<uint64_t> pending_reset_snapshot_generation{0};
    std::atomic<uint64_t> last_reset_snapshot_generation{0};
    QAtomicInteger<int> stream_session_active = 0;
    QAtomicInteger<int> snapshot_replay_allowed = 0;
    QAtomicInteger<int> held_frame_clear_pending = 0;
    QMutex kept_frame_mutex;
    AVFrame *kept_frame = nullptr;
    double kept_frame_pts = 0.0;
    float kept_frame_duration = 0.0f;
    AVFrame *fallback_frame = nullptr;
    double fallback_frame_pts = 0.0;
    float fallback_frame_duration = 0.0f;
    bool fallback_frame_valid = false;
    bool held_frame_valid = false;
    QAtomicInteger<int> swapchain_recreate_pending = 0;
    QAtomicInteger<int> renderer_cache_flush_pending = 0;
    QAtomicInteger<int> placebo_reset_pending = 0;
    QAtomicInteger<int> recovery_hold_pending = 0;
    QAtomicInteger<int> render_active = 0;
    bool present_vsync_enabled = true;

    QVulkanInstance *qt_vk_inst = {};
    QOpenGLContext *qt_gl_context = {};
    QOffscreenSurface *qt_gl_offscreen_surface = {};
    QQmlEngine *qml_engine = {};
    QQuickWindow *quick_window = {};
    QQuickRenderControl *quick_render = {};
    QQuickItem *quick_item = {};
    pl_tex quick_tex = {};
    QOpenGLFramebufferObject *quick_fbo = {};
    VkSemaphore quick_sem = VK_NULL_HANDLE;
    uint64_t quick_sem_value = 0;
    QTimer *update_timer = {};
    bool quick_frame = false;
    QAtomicInteger<int> quick_need_sync = 0;
    QAtomicInteger<int> quick_need_render = 0;
    QString pending_renderer_fallback_reason;
    pl_options renderparams_opts = {};
    bool renderparams_changed = false;
    const struct pl_hook *fsr_hook = nullptr;
    const struct pl_hook *fsrcnnx_hook_8 = nullptr;
    const struct pl_hook *fsrcnnx_hook_16 = nullptr;

    struct {
        PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
#if defined(Q_OS_LINUX)
        PFN_vkCreateXcbSurfaceKHR vkCreateXcbSurfaceKHR;
        PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;
#elif defined(Q_OS_MACOS)
        PFN_vkCreateMetalSurfaceEXT vkCreateMetalSurfaceEXT;
#elif defined(Q_OS_WIN)
        PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
#endif
        PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
        PFN_vkWaitSemaphores vkWaitSemaphores;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
        PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
    } vk_funcs;

    friend class QmlBackend;
};
