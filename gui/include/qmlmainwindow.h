#pragma once

#include "streamsession.h"

#include <QMutex>
#include <QWindow>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QLoggingCategory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libplacebo/options.h>
#include <libplacebo/vulkan.h>
#include <libplacebo/renderer.h>
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

class QmlMainWindow : public QWindow
{
    Q_OBJECT
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    Q_PROPERTY(int corruptedFrames READ corruptedFrames NOTIFY corruptedFramesChanged)
    Q_PROPERTY(bool keepVideo READ keepVideo WRITE setKeepVideo NOTIFY keepVideoChanged)
    Q_PROPERTY(bool grabInput READ grabInput WRITE setGrabInput NOTIFY grabInputChanged)
    Q_PROPERTY(VideoMode videoMode READ videoMode WRITE setVideoMode NOTIFY videoModeChanged)
    Q_PROPERTY(VideoPreset videoPreset READ videoPreset WRITE setVideoPreset NOTIFY videoPresetChanged)

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
        HighQuality
    };
    Q_ENUM(VideoPreset);

    QmlMainWindow(Settings *settings);
    QmlMainWindow(const StreamSessionConnectInfo &connect_info);
    ~QmlMainWindow();

    bool hasVideo() const;
    int corruptedFrames() const;

    bool keepVideo() const;
    void setKeepVideo(bool keep);

    bool grabInput() const;
    void setGrabInput(bool grab);

    VideoMode videoMode() const;
    void setVideoMode(VideoMode mode);

    VideoPreset videoPreset() const;
    void setVideoPreset(VideoPreset mode);

    void show();
    void presentFrame(AVFrame *frame);

    static QSurfaceFormat createSurfaceFormat();

signals:
    void hasVideoChanged();
    void corruptedFramesChanged();
    void keepVideoChanged();
    void grabInputChanged();
    void videoModeChanged();
    void videoPresetChanged();

private:
    void init(Settings *settings);
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
    bool handleShortcut(QKeyEvent *event);
    bool event(QEvent *event) override;
    QObject *focusObject() const override;

    bool has_video = false;
    bool keep_video = false;
    bool grab_input = false;
    int corrupted_frames = 0;
    VideoMode video_mode = VideoMode::Normal;
    VideoPreset video_preset = VideoPreset::HighQuality;

    QmlBackend *backend = {};
    StreamSession *session = {};

    pl_cache placebo_cache = {};
    pl_log placebo_log = {};
    pl_vk_inst placebo_vk_inst = {};
    pl_vulkan placebo_vulkan = {};
    pl_swapchain placebo_swapchain = {};
    pl_renderer placebo_renderer = {};
    pl_tex placebo_tex[4] = {{}, {}, {}, {}};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    QSize swapchain_size;
    QMutex frame_mutex;
    QThread *render_thread = {};
    AVFrame *current_frame = {};
    AVFrame *next_frame = {};
    std::atomic<bool> render_scheduled = {false};

    QVulkanInstance *qt_vk_inst = {};
    QQmlEngine *qml_engine = {};
    QQuickWindow *quick_window = {};
    QQuickRenderControl *quick_render = {};
    QQuickItem *quick_item = {};
    pl_tex quick_tex = {};
    VkSemaphore quick_sem = VK_NULL_HANDLE;
    uint64_t quick_sem_value = 0;
    QTimer *update_timer = {};
    bool quick_frame = false;
    bool quick_need_sync = false;
    std::atomic<bool> quick_need_render = {false};

    struct {
        PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
#if defined(Q_OS_LINUX)
        PFN_vkCreateXcbSurfaceKHR vkCreateXcbSurfaceKHR;
        PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;
#elif defined(Q_OS_MACOS)
        PFN_vkCreateMetalSurfaceEXT vkCreateMetalSurfaceEXT;
#elif defined(Q_OS_WIN32)
        PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
#endif
        PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
        PFN_vkWaitSemaphores vkWaitSemaphores;
    } vk_funcs;

    friend class QmlBackend;
};
