#include "qmlmainwindow.h"
#include "qmlbackend.h"
#include "qmlsvgprovider.h"
#include "chiaki/log.h"
#include "chiaki/time.h"
#include "streamsession.h"

#include <qpa/qplatformnativeinterface.h>

#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>

#include <QDebug>
#include <QThread>
#include <QShortcut>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QFile>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QVulkanInstance>
#include <QQuickItem>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickRenderTarget>
#include <QQuickRenderControl>
#include <QQuickGraphicsDevice>
#if defined(Q_OS_MACOS)
#include <objc/message.h>
#endif

Q_LOGGING_CATEGORY(chiakiGui, "chiaki.gui", QtInfoMsg);

static void detectOpenGLGpuVendor(QmlMainWindow *window)
{
    auto *ctx = QOpenGLContext::currentContext();
    if (!ctx)
        return;

    const GLubyte *vendor_raw = ctx->extraFunctions()->glGetString(GL_VENDOR);
    if (!vendor_raw)
        return;

    QByteArray vendor = QByteArray(reinterpret_cast<const char *>(vendor_raw)).toLower();
    if (vendor.contains("amd") || vendor.contains("ati"))
    {
        window->setProperty("_chiaki_detected_amd", true);
        qCInfo(chiakiGui) << "Using amd graphics card";
    }
    else if (vendor.contains("nvidia"))
    {
        window->setProperty("_chiaki_detected_nvidia", true);
        qCInfo(chiakiGui) << "Using nvidia graphics card";
    }
}

static void placebo_log_cb(void *user, pl_log_level level, const char *msg)
{
    ChiakiLogLevel chiaki_level;
    switch (level) {
    case PL_LOG_NONE:
    case PL_LOG_TRACE:
    case PL_LOG_DEBUG:
        qCDebug(chiakiGui).noquote() << "[libplacebo]" << msg;
        break;
    case PL_LOG_INFO:
        qCInfo(chiakiGui).noquote() << "[libplacebo]" << msg;
        break;
    case PL_LOG_WARN:
        qCWarning(chiakiGui).noquote() << "[libplacebo]" << msg;
        break;
    case PL_LOG_ERR:
    case PL_LOG_FATAL:
        qCCritical(chiakiGui).noquote() << "[libplacebo]" << msg;
        break;
    }
}

static bool map_frame(pl_gpu gpu, pl_tex *tex,
                      const struct pl_source_frame *src,
                      struct pl_frame *out_frame)
{
    AVFrame *frame = reinterpret_cast<AVFrame *>(src->frame_data);
    QmlMainWindow *q = reinterpret_cast<QmlMainWindow *>(frame->opaque);
    pl_avframe_params av_params{
        .frame      = frame,
        .tex        = tex,};
    bool ok = pl_map_avframe_ex(gpu, out_frame, &av_params
    );

    if (!ok) {
        fprintf(stderr, "Failed mapping AVFrame!\n");
        qCWarning(chiakiGui) << "Failed to map AVFrame to Placebo frame!";
        if(q->getBackend() && q->getBackend()->zeroCopy())
        {
            qCInfo(chiakiGui) << "Mapping frame failed, trying without zero copy!";
            q->getBackend()->disableZeroCopy();
        }
        av_frame_free(&frame);
        return false;
    }
    return true;
}

static void unmap_frame(pl_gpu gpu, struct pl_frame *frame,
                        const struct pl_source_frame *src)
{
    pl_unmap_avframe(gpu, frame);
    AVFrame *av_frame = reinterpret_cast<AVFrame *>(src->frame_data);
    av_frame_free(&av_frame);
}

static void discard_frame(const struct pl_source_frame *src)
{
    AVFrame *frame = reinterpret_cast<AVFrame *>(src->frame_data);
    QmlMainWindow *q = reinterpret_cast<QmlMainWindow *>(frame->opaque);
    q->increaseDroppedFrames();
    av_frame_free(&frame);
    qCDebug(chiakiGui) << "Dropped frame with PTS" << src->pts;
}

static QString shader_cache_path()
{
    static QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pl_shader.cache";
    return path;
}


static const char *render_params_path()
{
    static QString path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/Chiaki/pl_render_params.conf";
    return qPrintable(path);
}

static const struct pl_hook *load_mpv_hook(pl_gpu gpu, const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(chiakiGui) << "Failed to open shader resource" << path;
        return nullptr;
    }

    QByteArray shader = file.readAll();
    const struct pl_hook *hook = pl_mpv_user_shader_parse(gpu, shader.constData(), static_cast<size_t>(shader.size()));
    if (!hook)
        qCWarning(chiakiGui) << "Failed to parse shader resource" << path;
    return hook;
}

static bool uses_fsrcnnx(PlaceboUpscaler upscaler)
{
    return upscaler == PlaceboUpscaler::FSRCNNX8 || upscaler == PlaceboUpscaler::FSRCNNX16;
}

static bool uses_custom_upscale_hook(PlaceboUpscaler upscaler)
{
    return upscaler == PlaceboUpscaler::FSR || uses_fsrcnnx(upscaler);
}

static const struct pl_hook *selected_fsrcnnx_hook(PlaceboUpscaler upscaler,
                                                   const struct pl_hook *hook8,
                                                   const struct pl_hook *hook16)
{
    switch (upscaler) {
    case PlaceboUpscaler::FSRCNNX8:
        return hook8;
    case PlaceboUpscaler::FSRCNNX16:
        return hook16;
    default:
        return nullptr;
    }
}

static void clearQuickOpenGLTarget(QOpenGLFramebufferObject *fbo)
{
    if (!fbo)
        return;

    auto *ctx = QOpenGLContext::currentContext();
    if (!ctx)
        return;

    QOpenGLExtraFunctions *gl = ctx->extraFunctions();
    if (!gl)
        return;

    gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo->handle()));
    gl->glViewport(0, 0, fbo->width(), fbo->height());
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

pl_gpu QmlMainWindow::placeboGpu() const
{
    if (placebo_vulkan)
        return placebo_vulkan->gpu;
    if (placebo_opengl)
        return placebo_opengl->gpu;
    return nullptr;
}

class RenderControl : public QQuickRenderControl
{
public:
    explicit RenderControl(QWindow *window)
        : window(window)
    {
    }

    QWindow *renderWindow(QPoint *offset) override
    {
        Q_UNUSED(offset);
        return window;
    }

private:
    QWindow *window = {};
};

QmlMainWindow::QmlMainWindow(Settings *settings, bool exit_app_on_stream_exit)
    : QWindow()
    , settings(settings)
{
    init(settings, exit_app_on_stream_exit);
}

QmlMainWindow::QmlMainWindow(const StreamSessionConnectInfo &connect_info)
    : QWindow()
    , settings(connect_info.settings)
{
    direct_stream = true;
    emit directStreamChanged();
    init(connect_info.settings);
    backend->createSession(connect_info);

    if (connect_info.fullscreen || connect_info.zoom || connect_info.stretch)
        fullscreenTime();
    if (connect_info.zoom)
        setVideoMode(VideoMode::Zoom);
    else if (connect_info.stretch)
        setVideoMode(VideoMode::Stretch);

    connect(session, &StreamSession::SessionQuit, qGuiApp, &QGuiApplication::quit);
}

QmlMainWindow::~QmlMainWindow()
{
    Q_ASSERT(!placebo_swapchain);

    if (render_backend == RenderBackend::Vulkan)
        av_buffer_unref(&vulkan_hw_dev_ctx);

    const bool isOpenGlBackend = render_backend == RenderBackend::OpenGL;
    bool hasOpenGlContextCurrent = isOpenGlBackend && makeOpenGLContextCurrent();

    if (isOpenGlBackend && quick_window)
        quick_window->setRenderTarget(QQuickRenderTarget());

#ifndef Q_OS_MACOS
    if (owns_render_thread) {
        QMetaObject::invokeMethod(quick_render, &QQuickRenderControl::invalidate);
        render_thread->quit();
        render_thread->wait();
    } else {
        quick_render->invalidate();
    }
#else
    // On macOS, we still need to invalidate to prevent leaks
    if (owns_render_thread) {
        QMetaObject::invokeMethod(quick_render, &QQuickRenderControl::invalidate);
        render_thread->quit();
        render_thread->wait();
    } else {
        quick_render->invalidate();
    }
#endif

    if (pl_gpu gpu = placeboGpu()) {
        pl_tex_destroy(gpu, &quick_tex);
        for (auto &tex : placebo_tex)
            pl_tex_destroy(gpu, &tex);
        if (render_backend == RenderBackend::Vulkan)
            pl_vulkan_sem_destroy(gpu, &quick_sem);
    }

    // Only destroy the FBO while its OpenGL context is current.
    if (hasOpenGlContextCurrent) {
        delete quick_fbo;
    } else if (quick_fbo) {
        // Try one more time to make context current for proper cleanup
        if (isOpenGlBackend && makeOpenGLContextCurrent()) {
            delete quick_fbo;
            hasOpenGlContextCurrent = true;
        }
    }
    quick_fbo = nullptr;

    if (hasOpenGlContextCurrent)
        doneOpenGLContextCurrent();

    delete quick_item;
    delete quick_window;
    // calls invalidate here if not already called
    delete quick_render;
    if (owns_render_thread)
        delete render_thread->parent();
    delete qml_engine;

    FILE *file = fopen(qPrintable(shader_cache_path()), "wb");
    if (file) {
        pl_cache_save_file(placebo_cache, file);
        fclose(file);
    }
    pl_cache_destroy(&placebo_cache);
    pl_queue_destroy(&placebo_queue);
    pl_renderer_destroy(&placebo_renderer);
    if (render_backend == RenderBackend::Vulkan) {
        pl_vulkan_destroy(&placebo_vulkan);
        pl_vk_inst_destroy(&placebo_vk_inst);
    } else {
        pl_opengl_destroy(&placebo_opengl);
    }
    delete qt_vk_inst;
    delete qt_gl_offscreen_surface;
    delete qt_gl_context;
    pl_options_free(&renderparams_opts);
    pl_mpv_user_shader_destroy(&fsr_hook);
    pl_mpv_user_shader_destroy(&fsrcnnx_hook_8);
    pl_mpv_user_shader_destroy(&fsrcnnx_hook_16);
    pl_log_destroy(&placebo_log);
}

void QmlMainWindow::updateWindowType(WindowType type)
{
    switch (type) {
    case WindowType::SelectedResolution:
        setVideoMode(VideoMode::Normal);
        break;
    case WindowType::CustomResolution:
        setVideoMode(VideoMode::Normal);
        break;
    case WindowType::Fullscreen:
        fullscreenTime();
        setVideoMode(VideoMode::Normal);
        break;
    case WindowType::Zoom:
        fullscreenTime();
        setVideoMode(VideoMode::Zoom);
        break;
    case WindowType::Stretch:
        fullscreenTime();
        setVideoMode(VideoMode::Stretch);
        break;
    default:
        break;
    }
}

bool QmlMainWindow::hasVideo() const
{
    return has_video;
}

int QmlMainWindow::droppedFrames() const
{
    return dropped_frames;
}

void QmlMainWindow::increaseDroppedFrames()
{
    dropped_frames_current.fetchAndAddRelaxed(1);
}

bool QmlMainWindow::keepVideo() const
{
    return keep_video;
}

QmlBackend * QmlMainWindow::getBackend()
{
    return backend;
}

void QmlMainWindow::setKeepVideo(bool keep)
{
    keep_video = keep;
    emit keepVideoChanged();
}

void QmlMainWindow::grabInput()
{
    setCursor(Qt::ArrowCursor);
    grab_input++;
    if (session)
        session->BlockInput(grab_input);
}

void QmlMainWindow::releaseInput()
{
    if (!grab_input)
        return;
    grab_input--;
    if (!grab_input && has_video && settings->GetHideCursor())
        setCursor(Qt::BlankCursor);
    if (session)
        session->BlockInput(grab_input);
}

bool QmlMainWindow::directStream() const
{
    return direct_stream;
}

QmlMainWindow::VideoMode QmlMainWindow::videoMode() const
{
    return video_mode;
}

void QmlMainWindow::setVideoMode(VideoMode mode)
{
    video_mode = mode;
    emit videoModeChanged();
}

float QmlMainWindow::zoomFactor() const
{
    return zoom_factor;
}

void QmlMainWindow::setZoomFactor(float factor)
{
    zoom_factor = factor;
    emit zoomFactorChanged();
}

QmlMainWindow::VideoPreset QmlMainWindow::videoPreset() const
{
    return video_preset;
}

void QmlMainWindow::setVideoPreset(VideoPreset preset)
{
    video_preset = preset;
    emit videoPresetChanged();
}

void QmlMainWindow::setSettings(Settings *new_settings)
{
    settings = new_settings;
    QString profile = settings->GetCurrentProfile();
    qCCritical(chiakiGui) << "Current Profile: " << profile;
    if(profile.isEmpty())
        QGuiApplication::setApplicationDisplayName("chiaki-ng");
    else
        QGuiApplication::setApplicationDisplayName(QString("chiaki-ng:%1").arg(profile));
    this->setTitle(QGuiApplication::applicationDisplayName());
}

void QmlMainWindow::show()
{
    QQmlComponent component(qml_engine, QUrl(QStringLiteral("qrc:/Main.qml")));
    if (!component.isReady()) {
        qCCritical(chiakiGui) << "Component not ready\n" << component.errors();
        QMetaObject::invokeMethod(QGuiApplication::instance(), &QGuiApplication::quit, Qt::QueuedConnection);
        return;
    }

    QVariantMap props;
    props[QStringLiteral("parent")] = QVariant::fromValue(quick_window->contentItem());
    quick_item = qobject_cast<QQuickItem*>(component.createWithInitialProperties(props));
    if (!quick_item) {
        qCCritical(chiakiGui) << "Failed to create root item\n" << component.errors();
        QMetaObject::invokeMethod(QGuiApplication::instance(), &QGuiApplication::quit, Qt::QueuedConnection);
        return;
    }
    if (!pending_renderer_fallback_reason.isEmpty()) {
        const QString reason = pending_renderer_fallback_reason;
        pending_renderer_fallback_reason.clear();
        QMetaObject::invokeMethod(quick_item, "showRendererFallbackDialog",
                                  Q_ARG(QVariant, QVariant::fromValue(reason)));
    }
    auto screen_size = QGuiApplication::primaryScreen()->availableSize();
    resize(screen_size);

    if (qEnvironmentVariable("XDG_CURRENT_DESKTOP") == "gamescope")
        fullscreenTime();
    else
    {
        if(!settings->GetGeometry().isEmpty())
        {
            setGeometry(settings->GetGeometry());
            showNormal();
        }
        else
            showMaximized();
        setWindowAdjustable(true);
    }
}

void QmlMainWindow::presentFrame(ChiakiFfmpegFrame frame, int32_t frames_lost)
{
    dropped_frames_current.fetchAndAddRelaxed(frames_lost);

    if (!frame.frame)
        return;

    frame.frame->opaque = this;

    if (queue_pts_origin < 0.0 || frame.pts + 1e-6 < queue_pts_origin) {
        pl_queue_reset(placebo_queue);
        ts_start = 0;
        queue_pts_origin = frame.pts;
        playback_started = false;
    }

    struct pl_source_frame src_frame{
        .pts = frame.pts - queue_pts_origin,
        .duration = static_cast<float>(frame.duration),
        // allow soft-disabling deinterlacing at the source frame level
        .first_field = this->renderparams_opts->params.deinterlace_params
            ? pl_field_from_avframe(frame.frame)
                : PL_FIELD_NONE,
        .frame_data = frame.frame,
        .map = map_frame,
        .unmap = unmap_frame,
        .discard = discard_frame,
    };
    pl_queue_push(placebo_queue, &src_frame);

    if (!has_video) {
        has_video = true;
        if (!grab_input && settings->GetHideCursor())
            setCursor(Qt::BlankCursor);
        emit hasVideoChanged();
    }
    scheduleUpdate();
}

AVBufferRef *QmlMainWindow::vulkanHwDeviceCtx()
{
    if (render_backend != RenderBackend::Vulkan)
        return nullptr;

    if (vulkan_hw_dev_ctx || vk_decode_queue_index < 0)
        return vulkan_hw_dev_ctx;

    vulkan_hw_dev_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
    AVHWDeviceContext *hwctx = reinterpret_cast<AVHWDeviceContext*>(vulkan_hw_dev_ctx->data);
    hwctx->user_opaque = const_cast<void*>(reinterpret_cast<const void*>(placebo_vulkan));
    AVVulkanDeviceContext *vkctx = reinterpret_cast<AVVulkanDeviceContext*>(hwctx->hwctx);
    vkctx->get_proc_addr = placebo_vulkan->get_proc_addr;
    vkctx->inst = placebo_vulkan->instance;
    vkctx->phys_dev = placebo_vulkan->phys_device;
    vkctx->act_dev = placebo_vulkan->device;
    vkctx->device_features = *placebo_vulkan->features;
    vkctx->enabled_inst_extensions = placebo_vk_inst->extensions;
    vkctx->nb_enabled_inst_extensions = placebo_vk_inst->num_extensions;
    vkctx->enabled_dev_extensions = placebo_vulkan->extensions;
    vkctx->nb_enabled_dev_extensions = placebo_vulkan->num_extensions;
    vkctx->queue_family_index = placebo_vulkan->queue_graphics.index;
    vkctx->nb_graphics_queues = placebo_vulkan->queue_graphics.count;
    vkctx->queue_family_tx_index = placebo_vulkan->queue_transfer.index;
    vkctx->nb_tx_queues = placebo_vulkan->queue_transfer.count;
    vkctx->queue_family_comp_index = placebo_vulkan->queue_compute.index;
    vkctx->nb_comp_queues = placebo_vulkan->queue_compute.count;
    vkctx->queue_family_decode_index = vk_decode_queue_index;
    vkctx->nb_decode_queues = 1;
    vkctx->lock_queue = [](struct AVHWDeviceContext *dev_ctx, uint32_t queue_family, uint32_t index) {
        auto vk = reinterpret_cast<pl_vulkan>(dev_ctx->user_opaque);
        vk->lock_queue(vk, queue_family, index);
    };
    vkctx->unlock_queue = [](struct AVHWDeviceContext *dev_ctx, uint32_t queue_family, uint32_t index) {
        auto vk = reinterpret_cast<pl_vulkan>(dev_ctx->user_opaque);
        vk->unlock_queue(vk, queue_family, index);
    };
    if (av_hwdevice_ctx_init(vulkan_hw_dev_ctx) < 0) {
        qCWarning(chiakiGui) << "Failed to create Vulkan decode context";
        av_buffer_unref(&vulkan_hw_dev_ctx);
    }

    return vulkan_hw_dev_ctx;
}

bool QmlMainWindow::makeOpenGLContextCurrent()
{
    if (!qt_gl_context)
        return false;

    QSurface *target_surface = nullptr;
    if (qt_gl_offscreen_surface && !isExposed())
        target_surface = qt_gl_offscreen_surface;
    else if (handle())
        target_surface = this;
    else if (qt_gl_offscreen_surface)
        target_surface = qt_gl_offscreen_surface;

    if (!target_surface)
        return false;

    if (QThread::currentThread() == qt_gl_context->thread())
        return qt_gl_context->makeCurrent(target_surface);

    bool ok = false;
    QObject *context_obj = qt_gl_context;
    QMetaObject::invokeMethod(context_obj, [this, target_surface, &ok]() {
        ok = qt_gl_context && qt_gl_context->makeCurrent(target_surface);
    }, Qt::BlockingQueuedConnection);
    return ok;
}

void QmlMainWindow::doneOpenGLContextCurrent()
{
    if (!qt_gl_context)
        return;

    if (QThread::currentThread() == qt_gl_context->thread()) {
        qt_gl_context->doneCurrent();
        return;
    }

    QObject *context_obj = qt_gl_context;
    QMetaObject::invokeMethod(context_obj, [this]() {
        if (qt_gl_context)
            qt_gl_context->doneCurrent();
    }, Qt::BlockingQueuedConnection);
}

void QmlMainWindow::init(Settings *settings, bool exit_app_on_stream_exit)
{
    render_backend = settings->GetRenderBackend();
    setSurfaceType(render_backend == RenderBackend::Vulkan ? QWindow::VulkanSurface : QWindow::OpenGLSurface);
    qparams = {};
    qparams.drift_compensation = 1e-3;
    qparams.interpolation_threshold = 0.01;
    qparams.timeout = 0;

    auto initOpenGLBackend = [&]() -> bool {
        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setAlphaBufferSize(8);
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
#if defined(Q_OS_MACOS)
        // macOS only exposes modern OpenGL as a core profile.
        if (format.majorVersion() < 4) {
            format.setVersion(4, 1);
            format.setProfile(QSurfaceFormat::CoreProfile);
        }
#endif

        qt_gl_context = new QOpenGLContext();
        qt_gl_context->setFormat(format);
        if (QOpenGLContext *share = QOpenGLContext::globalShareContext())
            qt_gl_context->setShareContext(share);
        if (!qt_gl_context->create()) {
            qCWarning(chiakiGui) << "Failed to create QOpenGLContext";
            return false;
        }

        setFormat(qt_gl_context->format());
        qt_gl_offscreen_surface = new QOffscreenSurface();
        qt_gl_offscreen_surface->setFormat(qt_gl_context->format());
        qt_gl_offscreen_surface->create();
        if (!qt_gl_offscreen_surface->isValid()) {
            qCWarning(chiakiGui) << "Failed to create OpenGL offscreen surface";
            return false;
        }

        if (!this->handle())
            this->create();

        if (!makeOpenGLContextCurrent()) {
            qCWarning(chiakiGui) << "Failed to make QOpenGLContext current";
            return false;
        }

        detectOpenGLGpuVendor(this);
        amd_card = property("_chiaki_detected_amd").toBool();
        nvidia_card = property("_chiaki_detected_nvidia").toBool();

        struct pl_opengl_params gl_params = {
            .get_proc_addr_ex = [](void *proc_ctx, const char *procname) -> pl_voidfunc_t {
                auto ctx = static_cast<QOpenGLContext *>(proc_ctx);
                // Use volatile to prevent compiler optimizations from caching the address on macOS.
                volatile pl_voidfunc_t addr = reinterpret_cast<pl_voidfunc_t>(ctx->getProcAddress(procname));
                return addr;
            },
            .proc_ctx = qt_gl_context,
            .debug = false,
            .allow_software = true,
            .make_current = [](void *priv) -> bool {
                auto q = static_cast<QmlMainWindow *>(priv);
                return q->makeOpenGLContextCurrent();
            },
            .release_current = [](void *priv) {
                auto q = static_cast<QmlMainWindow *>(priv);
                q->doneOpenGLContextCurrent();
            },
            .priv = this,
        };
        placebo_opengl = pl_opengl_create(placebo_log, &gl_params);
        if (!placebo_opengl) {
            qCWarning(chiakiGui) << "Failed to create libplacebo OpenGL backend";
            return false;
        }
        doneOpenGLContextCurrent();
        return true;
    };

    auto fallbackToOpenGL = [&](const QString &reason) {
        qCWarning(chiakiGui) << "Vulkan renderer initialization failed, falling back to OpenGL:" << reason;
        pending_renderer_fallback_reason = reason;

        if (placebo_vulkan)
            pl_vulkan_destroy(&placebo_vulkan);
        if (placebo_vk_inst)
            pl_vk_inst_destroy(&placebo_vk_inst);

        render_backend = RenderBackend::OpenGL;
        setSurfaceType(QWindow::OpenGLSurface);
        if (!initOpenGLBackend())
            qFatal("Failed initializing OpenGL backend");
    };

    struct pl_log_params log_params = {
        .log_cb = placebo_log_cb,
        .log_level = PL_LOG_DEBUG,
    };
    placebo_log = pl_log_create(PL_API_VER, &log_params);

    if (render_backend == RenderBackend::Vulkan) {
        if (qEnvironmentVariableIsSet("CHIAKI_FORCE_VULKAN_FALLBACK")) {
            fallbackToOpenGL(QStringLiteral("Forced Vulkan fallback for testing"));
            goto renderer_backend_ready;
        }

        const char *vk_exts[] = {
            nullptr,
            VK_KHR_SURFACE_EXTENSION_NAME,
        };

#if defined(Q_OS_LINUX)
        if (QGuiApplication::platformName().startsWith("wayland"))
            vk_exts[0] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
        else if (QGuiApplication::platformName().startsWith("xcb"))
            vk_exts[0] = VK_KHR_XCB_SURFACE_EXTENSION_NAME;
        else
            Q_UNREACHABLE();
#elif defined(Q_OS_MACOS)
        vk_exts[0] = VK_EXT_METAL_SURFACE_EXTENSION_NAME;
#elif defined(Q_OS_WIN)
        vk_exts[0] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#endif

        const char *opt_extensions[] = {
            VK_EXT_HDR_METADATA_EXTENSION_NAME,
        };

        struct pl_vk_inst_params vk_inst_params = {
            .extensions = vk_exts,
            .num_extensions = 2,
            .opt_extensions = opt_extensions,
            .num_opt_extensions = 1,
        };
        placebo_vk_inst = pl_vk_inst_create(placebo_log, &vk_inst_params);
        if (!placebo_vk_inst) {
            fallbackToOpenGL(QStringLiteral("Failed to create Vulkan instance"));
        } else {

#define GET_PROC(name_) { \
    vk_funcs.name_ = reinterpret_cast<decltype(vk_funcs.name_)>( \
        placebo_vk_inst->get_proc_addr(placebo_vk_inst->instance, #name_)); \
    if (!vk_funcs.name_) { \
        fallbackToOpenGL(QStringLiteral("Failed to resolve Vulkan symbol %1").arg(#name_)); \
        goto vulkan_setup_done; \
    } }
        GET_PROC(vkGetDeviceProcAddr)
#if defined(Q_OS_LINUX)
        if (QGuiApplication::platformName().startsWith("wayland"))
            GET_PROC(vkCreateWaylandSurfaceKHR)
        else if (QGuiApplication::platformName().startsWith("xcb"))
            GET_PROC(vkCreateXcbSurfaceKHR)
#elif defined(Q_OS_MACOS)
        GET_PROC(vkCreateMetalSurfaceEXT)
#elif defined(Q_OS_WIN)
        GET_PROC(vkCreateWin32SurfaceKHR)
#endif
        GET_PROC(vkDestroySurfaceKHR)
        GET_PROC(vkGetPhysicalDeviceQueueFamilyProperties)
        GET_PROC(vkGetPhysicalDeviceProperties)
#undef GET_PROC

        const char *opt_dev_extensions[] = {
            VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
            VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME,
            VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME,
            VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME,
        };

        struct pl_vulkan_params vulkan_params = {
            .instance = placebo_vk_inst->instance,
            .get_proc_addr = placebo_vk_inst->get_proc_addr,
            .allow_software = true,
            PL_VULKAN_DEFAULTS
            .extra_queues = VK_QUEUE_VIDEO_DECODE_BIT_KHR,
            .opt_extensions = opt_dev_extensions,
            .num_opt_extensions = 4,
        };
        placebo_vulkan = pl_vulkan_create(placebo_log, &vulkan_params);
        if (!placebo_vulkan) {
            fallbackToOpenGL(QStringLiteral("Failed to create Vulkan device"));
        } else {

#define GET_PROC(name_) { \
    vk_funcs.name_ = reinterpret_cast<decltype(vk_funcs.name_)>( \
        vk_funcs.vkGetDeviceProcAddr(placebo_vulkan->device, #name_)); \
    if (!vk_funcs.name_) { \
        fallbackToOpenGL(QStringLiteral("Failed to resolve Vulkan symbol %1").arg(#name_)); \
        goto vulkan_setup_done; \
    } }
        GET_PROC(vkWaitSemaphores)
#undef GET_PROC

        uint32_t queueFamilyCount = 0;
        vk_funcs.vkGetPhysicalDeviceQueueFamilyProperties(placebo_vulkan->phys_device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
        vk_funcs.vkGetPhysicalDeviceQueueFamilyProperties(placebo_vulkan->phys_device, &queueFamilyCount, queueFamilyProperties.data());
        auto queue_it = std::find_if(queueFamilyProperties.begin(), queueFamilyProperties.end(), [](VkQueueFamilyProperties prop) {
            return prop.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR;
        });
        if (queue_it != queueFamilyProperties.end())
            vk_decode_queue_index = std::distance(queueFamilyProperties.begin(), queue_it);
        VkPhysicalDeviceProperties device_props;
        vk_funcs.vkGetPhysicalDeviceProperties(placebo_vulkan->phys_device, &device_props);
        if(device_props.vendorID == 0x1002)
        {
            amd_card = true;
            qCInfo(chiakiGui) << "Using amd graphics card";
        }
        else if(device_props.vendorID == 0x10de)
        {
            nvidia_card = true;
            qCInfo(chiakiGui) << "Using nvidia graphics card";
        }
        }
        }
vulkan_setup_done:
        ;
    } else {
        if (!initOpenGLBackend())
            qFatal("Failed initializing OpenGL backend");
    }

    if (render_backend == RenderBackend::Vulkan) {
        qt_vk_inst = new QVulkanInstance;
        qt_vk_inst->setVkInstance(placebo_vk_inst->instance);
        if (!qt_vk_inst->create()) {
            delete qt_vk_inst;
            qt_vk_inst = nullptr;
            fallbackToOpenGL(QStringLiteral("Failed to create QVulkanInstance"));
        }
    }

renderer_backend_ready:
    struct pl_cache_params cache_params = {
        .log = placebo_log,
        .max_total_size = 10 << 20, // 10 MB
    };
    placebo_cache = pl_cache_create(&cache_params);
    pl_gpu_set_cache(placeboGpu(), placebo_cache);
    FILE *file = fopen(qPrintable(shader_cache_path()), "rb");
    if (file) {
        pl_cache_load_file(placebo_cache, file);
        fclose(file);
    }

    placebo_renderer = pl_renderer_create(
        placebo_log,
        placeboGpu()
    );

    placebo_queue = pl_queue_create(placeboGpu());

    if (render_backend == RenderBackend::Vulkan) {
        struct pl_vulkan_sem_params sem_params = {
            .type = VK_SEMAPHORE_TYPE_TIMELINE,
        };
        quick_sem = pl_vulkan_sem_create(placebo_vulkan->gpu, &sem_params);
    }

    quick_render = new RenderControl(this);

    QQuickWindow::setDefaultAlphaBuffer(true);
    QQuickWindow::setGraphicsApi(render_backend == RenderBackend::Vulkan ? QSGRendererInterface::Vulkan : QSGRendererInterface::OpenGL);
    quick_window = new QQuickWindow(quick_render);
    if (render_backend == RenderBackend::Vulkan) {
        quick_window->setVulkanInstance(qt_vk_inst);
        quick_window->setGraphicsDevice(QQuickGraphicsDevice::fromDeviceObjects(placebo_vulkan->phys_device, placebo_vulkan->device, placebo_vulkan->queue_graphics.index));
    } else {
        quick_window->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(qt_gl_context));
    }
    quick_window->setColor(QColor(0, 0, 0, 0));
    connect(quick_window, &QQuickWindow::focusObjectChanged, this, &QmlMainWindow::focusObjectChanged);

    qml_engine = new QQmlEngine(this);
    qml_engine->addImageProvider(QStringLiteral("svg"), new QmlSvgProvider);
    if (!qml_engine->incubationController())
        qml_engine->setIncubationController(quick_window->incubationController());
    connect(qml_engine, &QQmlEngine::quit, this, &QWindow::close);

    backend = new QmlBackend(settings, this);
    connect(backend, &QmlBackend::sessionChanged, this, [this, exit_app_on_stream_exit](StreamSession *s) {
        session = s;
        grab_input = 0;
        if (session)
            session->BlockInput(0);
        update_timer->stop();
        pl_queue_reset(placebo_queue);
        queue_pts_origin = -1.0;
        playback_started = false;
        ts_start = 0;
        if (has_video) {
            has_video = false;
            setCursor(Qt::ArrowCursor);
            emit hasVideoChanged();
        }
        if(session && exit_app_on_stream_exit)
        {
            connect(session, &StreamSession::SessionQuit, qGuiApp, &QGuiApplication::quit);
        }
        if(!session)
        {
            setStreamWindowAdjustable(false);
            if(qEnvironmentVariable("XDG_CURRENT_DESKTOP") != "gamescope")
            {
                if(!this->settings->GetGeometry().isEmpty())
                {
                    setGeometry(this->settings->GetGeometry());
                    normalTime();
                }
                else
                    showMaximized();
            }
        }
    });
    connect(backend, &QmlBackend::windowTypeUpdated, this, &QmlMainWindow::updateWindowType);

    if (render_backend == RenderBackend::OpenGL) {
        render_thread = QGuiApplication::instance()->thread();
        owns_render_thread = false;
    } else {
        render_thread = new QThread;
        render_thread->setObjectName("render");
        render_thread->start();
        owns_render_thread = true;

        quick_render->prepareThread(render_thread);
        quick_render->moveToThread(render_thread);
    }

    connect(quick_render, &QQuickRenderControl::sceneChanged, this, [this]() {
        quick_need_sync = true;
        scheduleUpdate();
    });
    connect(quick_render, &QQuickRenderControl::renderRequested, this, [this]() {
        quick_need_render.storeRelaxed(1);
        scheduleUpdate();
    });

    update_timer = new QTimer(this);
    update_timer->setSingleShot(true);
    connect(update_timer, &QTimer::timeout, this, &QmlMainWindow::update);

    if (render_backend == RenderBackend::OpenGL) {
        if (!makeOpenGLContextCurrent())
            qFatal("Failed to make QOpenGLContext current for render control initialization");
        quick_render->initialize();
        doneOpenGLContextCurrent();
    } else {
        QMetaObject::invokeMethod(quick_render, &QQuickRenderControl::initialize);
    }

    QTimer *dropped_frames_timer = new QTimer(this);
    dropped_frames_timer->setInterval(1000);
    dropped_frames_timer->start();
    connect(dropped_frames_timer, &QTimer::timeout, this, [this]() {
        int dropped_frames_next = dropped_frames_current.fetchAndStoreRelaxed(0);
        if (dropped_frames != dropped_frames_next) {
            dropped_frames = dropped_frames_next;
            emit droppedFramesChanged();
        }
    });

    this->renderparams_opts = pl_options_alloc(this->placebo_log);
    pl_options_reset(this->renderparams_opts, &pl_render_high_quality_params);
    this->renderparams_changed = true;
    this->fsr_hook = load_mpv_hook(placeboGpu(), QStringLiteral(":/shaders/FSR.glsl"));
    this->fsrcnnx_hook_8 = load_mpv_hook(placeboGpu(), QStringLiteral(":/shaders/FSRCNNX_x2_8-0-4-1.glsl"));
    this->fsrcnnx_hook_16 = load_mpv_hook(placeboGpu(), QStringLiteral(":/shaders/FSRCNNX_x2_16-0-4-1.glsl"));


    switch (settings->GetPlaceboPreset()) {
    case PlaceboPreset::Fast:
        setVideoPreset(VideoPreset::Fast);
        break;
    case PlaceboPreset::Default:
        setVideoPreset(VideoPreset::Default);
        break;
    case PlaceboPreset::HighQualitySpatial:
        setVideoPreset(VideoPreset::HighQualitySpatial);
        break;
    case PlaceboPreset::HighQualityAdvancedSpatial:
        setVideoPreset(VideoPreset::HighQualityAdvancedSpatial);
        break;
    case PlaceboPreset::Custom:
        setVideoPreset(VideoPreset::Custom);
        break;
    case PlaceboPreset::HighQuality:
    default:
        setVideoPreset(VideoPreset::HighQuality);
        break;
    }
    setZoomFactor(settings->GetZoomFactor());
}

void QmlMainWindow::normalTime()
{
    if(windowState() == Qt::WindowFullScreen)
    {
        if(was_maximized)
        {
            showMaximized();
            was_maximized = false;
        }
        else
            showNormal();
        setMinimumSize(QSize(0, 0));
    }
    QTimer::singleShot(1000, this, [this]{
        if(session)
            setStreamWindowAdjustable(true);
        else
        {
            setWindowAdjustable(true);
        }
    });
}

void QmlMainWindow::fullscreenTime()
{
    if(windowState() == Qt::WindowFullScreen)
        return;
    if(session)
        setStreamWindowAdjustable(false);
    else
        setWindowAdjustable(false);
    setMinimumSize(size());
    if(windowState() == Qt::WindowMaximized)
        was_maximized = true;
    else
        was_maximized = false;
    showFullScreen();
}
void QmlMainWindow::update()
{
    Q_ASSERT(QThread::currentThread() == QGuiApplication::instance()->thread());

    {
        QMutexLocker locker(&render_schedule_mutex);
        if (render_scheduled) {
            render_pending = true;
            return;
        }
        render_scheduled = true;
    }

    if (quick_need_sync) {
        quick_need_sync = false;
        quick_render->polishItems();
        if (quick_render->thread() == QThread::currentThread())
            sync();
        else
            QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::sync, this), Qt::BlockingQueuedConnection);
    }

    update_timer->stop();
    QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::render, this));
}

void QmlMainWindow::scheduleUpdate()
{
    Q_ASSERT(QThread::currentThread() == QGuiApplication::instance()->thread());

    if (!update_timer->isActive()) {
        double refresh_rate = screen() ? screen()->refreshRate() : 60.0;
        if (refresh_rate <= 1.0)
            refresh_rate = 60.0;
        int interval_ms = has_video ? qMax(1, (int)(1000.0 / refresh_rate)) : 10;
        update_timer->start(interval_ms);
    }
}

void QmlMainWindow::updatePlacebo()
{
    renderparams_changed = true;
}

void QmlMainWindow::createSwapchain()
{
    Q_ASSERT(QThread::currentThread() == render_thread);

    if (placebo_swapchain)
        return;

    if (render_backend == RenderBackend::OpenGL) {
        if (!makeOpenGLContextCurrent()) {
            qCCritical(chiakiGui) << "Failed to make OpenGL context current";
            return;
        }
        struct pl_opengl_swapchain_params sw_params = {
            .swap_buffers = [](void *priv) {
                auto q = static_cast<QmlMainWindow *>(priv);
                if (q->qt_gl_context)
                    q->qt_gl_context->swapBuffers(q);
            },
            .priv = this,
        };
        placebo_swapchain = pl_opengl_create_swapchain(placebo_opengl, &sw_params);
        doneOpenGLContextCurrent();
        if (!placebo_swapchain)
            qCCritical(chiakiGui) << "Failed creating OpenGL swapchain";
        return;
    }

    VkResult err = VK_ERROR_UNKNOWN;
#if defined(Q_OS_LINUX)
    if (QGuiApplication::platformName().startsWith("wayland")) {
        VkWaylandSurfaceCreateInfoKHR surfaceInfo = {};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.display = reinterpret_cast<struct wl_display*>(QGuiApplication::platformNativeInterface()->nativeResourceForWindow("display", this));
        surfaceInfo.surface = reinterpret_cast<struct wl_surface*>(QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", this));
        err = vk_funcs.vkCreateWaylandSurfaceKHR(placebo_vk_inst->instance, &surfaceInfo, nullptr, &surface);
    } else if (QGuiApplication::platformName().startsWith("xcb")) {
        VkXcbSurfaceCreateInfoKHR surfaceInfo = {};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.connection = reinterpret_cast<xcb_connection_t*>(QGuiApplication::platformNativeInterface()->nativeResourceForWindow("connection", this));
        surfaceInfo.window = static_cast<xcb_window_t>(winId());
        err = vk_funcs.vkCreateXcbSurfaceKHR(placebo_vk_inst->instance, &surfaceInfo, nullptr, &surface);
    } else {
        Q_UNREACHABLE();
    }
#elif defined(Q_OS_MACOS)
    VkMetalSurfaceCreateInfoEXT surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    surfaceInfo.pLayer = static_cast<const CAMetalLayer*>(reinterpret_cast<void*(*)(id, SEL)>(objc_msgSend)(reinterpret_cast<id>(winId()), sel_registerName("layer")));
    err = vk_funcs.vkCreateMetalSurfaceEXT(placebo_vk_inst->instance, &surfaceInfo, nullptr, &surface);
#elif defined(Q_OS_WIN)
    VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = GetModuleHandle(nullptr);
    surfaceInfo.hwnd = reinterpret_cast<HWND>(winId());
    err = vk_funcs.vkCreateWin32SurfaceKHR(placebo_vk_inst->instance, &surfaceInfo, nullptr, &surface);
#endif

    if (err != VK_SUCCESS)
        qFatal("Failed to create VkSurfaceKHR");

    struct pl_vulkan_swapchain_params swapchain_params = {
        .surface = surface,
        .present_mode = VK_PRESENT_MODE_FIFO_KHR,
        .swapchain_depth = 1,
    };
    placebo_swapchain = pl_vulkan_create_swapchain(placebo_vulkan, &swapchain_params);
}

void QmlMainWindow::destroySwapchain()
{
    Q_ASSERT(QThread::currentThread() == render_thread);

    if (!placebo_swapchain)
        return;

    pl_swapchain_destroy(&placebo_swapchain);
    if (render_backend == RenderBackend::Vulkan)
        vk_funcs.vkDestroySurfaceKHR(placebo_vk_inst->instance, surface, nullptr);
    if (render_backend == RenderBackend::OpenGL && quick_fbo) {
        if (!makeOpenGLContextCurrent()) {
            qCWarning(chiakiGui) << "Failed to make OpenGL context current while destroying swapchain";
            swapchain_size = QSize();
            return;
        }
        delete quick_fbo;
        quick_fbo = nullptr;
        doneOpenGLContextCurrent();
    } else {
        delete quick_fbo;
        quick_fbo = nullptr;
    }
    swapchain_size = QSize();
}

void QmlMainWindow::resizeSwapchain()
{
    Q_ASSERT(QThread::currentThread() == render_thread);

    if (!placebo_swapchain)
        createSwapchain();
    if (!placebo_swapchain)
        return;

    const QSize window_size(width() * devicePixelRatio(), height() * devicePixelRatio());
    const bool swapchain_ready = quick_tex && (render_backend != RenderBackend::OpenGL || quick_fbo);
    if (window_size == swapchain_size && swapchain_ready)
        return;

    QSize new_swapchain_size = window_size;
    pl_swapchain_resize(placebo_swapchain, &new_swapchain_size.rwidth(), &new_swapchain_size.rheight());

    if (render_backend == RenderBackend::OpenGL) {
        if (!makeOpenGLContextCurrent()) {
            qCCritical(chiakiGui) << "Failed to make OpenGL context current";
            return;
        }

        QOpenGLFramebufferObject *new_quick_fbo =
            new QOpenGLFramebufferObject(new_swapchain_size, QOpenGLFramebufferObject::CombinedDepthStencil);
        if (!new_quick_fbo || !new_quick_fbo->isValid()) {
            qCCritical(chiakiGui) << "Failed to create Qt Quick OpenGL framebuffer object";
            delete new_quick_fbo;
            doneOpenGLContextCurrent();
            return;
        }

        int quick_texture_internal_format = static_cast<int>(new_quick_fbo->format().internalTextureFormat());
        if (!quick_texture_internal_format)
            quick_texture_internal_format = 0x8058; // GL_RGBA8
        struct pl_opengl_wrap_params wrap_params = {
            .texture = new_quick_fbo->texture(),
            .framebuffer = static_cast<unsigned int>(new_quick_fbo->handle()),
            .width = new_swapchain_size.width(),
            .height = new_swapchain_size.height(),
            .target = 0x0DE1,
            .iformat = quick_texture_internal_format,
        };
        pl_tex new_quick_tex = pl_opengl_wrap(placeboGpu(), &wrap_params);
        if (!new_quick_tex) {
            qCCritical(chiakiGui) << "Failed to wrap Qt Quick OpenGL framebuffer texture";
            delete new_quick_fbo;
            doneOpenGLContextCurrent();
            return;
        }

        quick_window->setRenderTarget(QQuickRenderTarget());
        if (quick_tex)
            pl_tex_destroy(placeboGpu(), &quick_tex);
        delete quick_fbo;
        quick_fbo = new_quick_fbo;
        quick_tex = new_quick_tex;
        swapchain_size = new_swapchain_size;
        quick_window->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(quick_fbo->texture(), quick_fbo->size()));
        doneOpenGLContextCurrent();
        return;
    }

    struct pl_tex_params tex_params = {
        .w = new_swapchain_size.width(),
        .h = new_swapchain_size.height(),
        .format = pl_find_fmt(placebo_vulkan->gpu, PL_FMT_UNORM, 4, 8, 8, PL_FMT_CAP_RENDERABLE),
        .sampleable = true,
        .renderable = true,
    };
    if (!pl_tex_recreate(placebo_vulkan->gpu, &quick_tex, &tex_params))
    {
        qCCritical(chiakiGui) << "Failed to create placebo texture";
        return;
    }

    VkFormat vk_format;
    VkImage vk_image = pl_vulkan_unwrap(placebo_vulkan->gpu, quick_tex, &vk_format, nullptr);
    swapchain_size = new_swapchain_size;
    quick_window->setRenderTarget(QQuickRenderTarget::fromVulkanImage(vk_image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, vk_format, swapchain_size));
}

void QmlMainWindow::updateSwapchain()
{
    Q_ASSERT(QThread::currentThread() == QGuiApplication::instance()->thread());

    quick_item->setSize(size());
    quick_window->resize(size());

    if (quick_render->thread() == QThread::currentThread())
        resizeSwapchain();
    else
        QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::resizeSwapchain, this), Qt::BlockingQueuedConnection);
    quick_render->polishItems();
    if (quick_render->thread() == QThread::currentThread())
        sync();
    else
        QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::sync, this), Qt::BlockingQueuedConnection);
    update();
}

void QmlMainWindow::sync()
{
    Q_ASSERT(QThread::currentThread() == render_thread);

    if (!quick_tex)
        return;

    beginFrame();
    if (!quick_frame)
        return;
    quick_need_render.storeRelaxed(quick_render->sync() ? 1 : 0);
}

void QmlMainWindow::beginFrame()
{
    if (quick_frame)
        return;

    if (render_backend == RenderBackend::OpenGL) {
        if (!makeOpenGLContextCurrent())
            return;
        quick_frame = true;
        quick_render->beginFrame();
        return;
    }

    struct pl_vulkan_hold_params hold_params = {
        .tex = quick_tex,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .qf = VK_QUEUE_FAMILY_IGNORED,
        .semaphore = {
            .sem = quick_sem,
            .value = ++quick_sem_value,
        }
    };
    pl_vulkan_hold_ex(placebo_vulkan->gpu, &hold_params);

    VkSemaphoreWaitInfo wait_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &quick_sem,
        .pValues = &quick_sem_value,
    };
    vk_funcs.vkWaitSemaphores(placebo_vulkan->device, &wait_info, UINT64_MAX);

    quick_frame = true;
    quick_render->beginFrame();
}

void QmlMainWindow::endFrame()
{
    if (!quick_frame)
        return;

    if (render_backend == RenderBackend::OpenGL) {
        quick_frame = false;
        quick_render->endFrame();
        doneOpenGLContextCurrent();
        return;
    }

    quick_frame = false;
    quick_render->endFrame();

    struct pl_vulkan_release_params release_params = {
        .tex = quick_tex,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .qf = VK_QUEUE_FAMILY_IGNORED,
    };
    pl_vulkan_release_ex(placebo_vulkan->gpu, &release_params);
}

void QmlMainWindow::render()
{
    Q_ASSERT(QThread::currentThread() == render_thread);

    bool schedule_next_update = false;
    auto finalize_render = [this]() {
        bool pending = false;
        {
            QMutexLocker locker(&render_schedule_mutex);
            render_scheduled = false;
            pending = render_pending;
            render_pending = false;
        }
        if (pending) {
            QMetaObject::invokeMethod(this, [this]() {
                update();
            }, Qt::QueuedConnection);
        }
    };

    if (!placebo_swapchain)
    {
        finalize_render();
        return;
    }

    const struct pl_render_params *render_params = &pl_render_default_params;
    const PlaceboUpscaler configured_upscaler = settings->GetPlaceboUpscaler();
    switch (video_preset) {
    case VideoPreset::Fast:
        render_params = &pl_render_fast_params;
        break;
    case VideoPreset::Default:
        render_params = &pl_render_default_params;
        pl_options_set_str(this->renderparams_opts, "deinterlace", "yes");
        if (render_params->deinterlace_params)
            this->renderparams_opts->params.deinterlace_params = render_params->deinterlace_params;
        break;
    case VideoPreset::HighQuality:
        render_params = &pl_render_high_quality_params;
        pl_options_set_str(this->renderparams_opts, "deinterlace", "yes");
        if (render_params->deinterlace_params)
            this->renderparams_opts->params.deinterlace_params = render_params->deinterlace_params;
        break;
    case VideoPreset::HighQualitySpatial:
        render_params = &pl_render_high_quality_params;
        pl_options_set_str(this->renderparams_opts, "deinterlace", "yes");
        if (render_params->deinterlace_params)
            this->renderparams_opts->params.deinterlace_params = render_params->deinterlace_params;
        break;
    case VideoPreset::HighQualityAdvancedSpatial:
        render_params = &pl_render_high_quality_params;
        pl_options_set_str(this->renderparams_opts, "deinterlace", "yes");
        if (render_params->deinterlace_params)
            this->renderparams_opts->params.deinterlace_params = render_params->deinterlace_params;
        break;
    case VideoPreset::Custom:
        if (renderparams_changed) {
            renderparams_changed = false;
            QMap<QString, QString> paramsData = settings->GetPlaceboValues();
            QMapIterator<QString, QString> i(paramsData);
            bool invalid_render_params = false;
            this->renderparams_opts->params = pl_render_default_params;
            pl_options_set_str(this->renderparams_opts, "deinterlace", "yes");
            if (pl_render_default_params.deinterlace_params)
                this->renderparams_opts->params.deinterlace_params = pl_render_default_params.deinterlace_params;
            while (i.hasNext()) {
                i.next();
                if ((i.key() == QLatin1String("upscaler") || i.key() == QLatin1String("plane_upscaler"))
                    && uses_custom_upscale_hook(configured_upscaler)) {
                    continue;
                }
                if(!pl_options_set_str(this->renderparams_opts, i.key().toUtf8().constData(), i.value().toUtf8().constData()))
                {
                    invalid_render_params = true;
                    qCCritical(chiakiGui) << "Failed to load custom render param: " << i.key() << " with value: " << i.value();
                }
            }
            if (invalid_render_params)
                qCInfo(chiakiGui) << "Updated custom render parameters with one or more invalid parameters.";
            else
                qCInfo(chiakiGui) << "Updated custom render parameters successfully.";
        }
        render_params = &(this->renderparams_opts->params);
        break;
    }

    struct pl_render_params params = *render_params;
    if (!params.frame_mixer)
        params.frame_mixer = pl_find_filter_config("oversample", PL_FILTER_FRAME_MIXING);

    uint64_t ts_pre_update = chiaki_time_now_monotonic_us();
    double refresh_rate = screen() ? screen()->refreshRate() : 60.0;
    if (refresh_rate <= 1.0)
        refresh_rate = 60.0;
    qparams.timeout = 0;
    qparams.radius = pl_frame_mix_radius(&params);
    qparams.vsync_duration = 1.0 / refresh_rate;
    qparams.pts = playback_started ? (double)(ts_pre_update - ts_start) / 1000000.0 : 0.0;
    enum pl_queue_status queue_status = pl_queue_update(placebo_queue, &frame_mix, &qparams);
    switch (queue_status) {
    case PL_QUEUE_ERR:
        finalize_render();
        return;
    case PL_QUEUE_EOF:
    case PL_QUEUE_OK:
    case PL_QUEUE_MORE:
        break;
    }

    if (render_backend == RenderBackend::OpenGL && quick_need_render.loadRelaxed()) {
        quick_need_render.storeRelaxed(0);
        beginFrame();
        if (quick_frame) {
            clearQuickOpenGLTarget(quick_fbo);
            quick_render->render();
        }
        endFrame();
    }

    struct pl_swapchain_frame sw_frame = {};
    if (!pl_swapchain_start_frame(placebo_swapchain, &sw_frame)) {
        qCWarning(chiakiGui) << "Failed to start Placebo frame!";
        finalize_render();
        return;
    }

    struct pl_color_space target_csp = sw_frame.color_space;
    if(!pl_color_transfer_is_hdr(target_csp.transfer))
    {
        target_csp.hdr.max_luma = 0;
        target_csp.hdr.min_luma = 0;
    }
    float target_peak = settings->GetDisplayTargetPeak() && session ? settings->GetDisplayTargetPeak() : 0;
    int target_contrast = settings->GetDisplayTargetContrast() && session ? settings->GetDisplayTargetContrast(): 0;
    int target_prim = settings->GetDisplayTargetPrim() && session ? settings->GetDisplayTargetPrim(): 0;
    int target_trc = settings->GetDisplayTargetTrc() && session ? settings->GetDisplayTargetTrc(): 0;
    struct pl_color_space hint = frame_mix.num_frames
        ? frame_mix.frames[frame_mix.num_frames - 1]->color
        : pl_color_space{};

    if(target_trc)
        hint.transfer = static_cast<pl_color_transfer>(target_trc);

    if(target_prim)
    {
        hint.primaries = static_cast<pl_color_primaries>(target_prim);
        hint.hdr.prim = *pl_raw_primaries_get(hint.primaries);
    }

    if(target_peak)
        hint.hdr.max_luma = target_peak;

    switch(target_contrast)
    {
        case -1:
            hint.hdr.min_luma = 1e-7;
            break;
        case 0:
            hint.hdr.min_luma = target_csp.hdr.min_luma;
            break;
        default:
            // Infer max_luma for current pl_color_space
            struct pl_nominal_luma_params hint_params = {};
            hint_params.color = &hint;
            hint_params.metadata = PL_HDR_METADATA_HDR10;
            hint_params.scaling = PL_HDR_NITS;
            hint_params.out_max = &hint.hdr.max_luma;
            pl_color_space_nominal_luma_ex(&hint_params);
            hint.hdr.min_luma = hint.hdr.max_luma / (float)target_contrast;
            break;
    }
    pl_swapchain_colorspace_hint(placebo_swapchain, &hint);

    struct pl_frame target_frame = {};
    pl_frame_from_swapchain(&target_frame, &sw_frame);
    struct pl_overlay_part overlay_part = {
        .src = {0, 0, static_cast<float>(swapchain_size.width()), static_cast<float>(swapchain_size.height())},
        .dst = {0, 0, static_cast<float>(swapchain_size.width()), static_cast<float>(swapchain_size.height())},
    };
    if (render_backend == RenderBackend::OpenGL) {
        overlay_part.src.y0 = static_cast<float>(swapchain_size.height());
        overlay_part.src.y1 = 0.0f;
    }
    struct pl_color_repr overlay_repr = pl_color_repr_rgb;
    overlay_repr.alpha = PL_ALPHA_PREMULTIPLIED;
    struct pl_overlay overlay = {
        .tex = quick_tex,
        .repr = overlay_repr,
        .color = pl_color_space_srgb,
        .parts = &overlay_part,
        .num_parts = 1,
    };
    target_frame.overlays = &overlay;
    target_frame.num_overlays = 1;
    if(target_prim)
    {
        target_frame.color.primaries = static_cast<pl_color_primaries>(target_prim);
        target_frame.color.hdr.prim = *pl_raw_primaries_get(target_frame.color.primaries);
    }

    if(target_trc)
        target_frame.color.transfer = static_cast<pl_color_transfer>(target_trc);

    if(target_peak && !target_frame.color.hdr.max_luma)
    {
        target_frame.color.hdr.max_luma = target_peak;
    }
    if(!target_frame.color.hdr.min_luma)
    {
        switch(target_contrast)
        {
            case -1:
                target_frame.color.hdr.min_luma = 1e-7;
                break;
            case 0:
                target_frame.color.hdr.min_luma = target_csp.hdr.min_luma;
                break;
            default:
                // Infer max_luma for current pl_color_space
                struct pl_nominal_luma_params target_params = {};
                target_params.color = &target_frame.color;
                target_params.metadata = PL_HDR_METADATA_HDR10;
                target_params.scaling = PL_HDR_NITS;
                target_params.out_max = &target_frame.color.hdr.max_luma;
                pl_color_space_nominal_luma_ex(&target_params);
                target_frame.color.hdr.min_luma = target_frame.color.hdr.max_luma / (float)target_contrast;
                break;
        }
    }
    if (quick_need_render.loadRelaxed()) {
        quick_need_render.storeRelaxed(0);
        beginFrame();
        if (quick_frame) {
            if (render_backend == RenderBackend::OpenGL)
                clearQuickOpenGLTarget(quick_fbo);
            quick_render->render();
        }
    }
    endFrame();

    if (frame_mix.num_frames) {
        pl_rect2df crop = frame_mix.frames[0]->crop;
        switch (video_mode) {
        case VideoMode::Normal:
            pl_rect2df_aspect_copy(&target_frame.crop, &crop, 0.0);
            break;
        case VideoMode::Stretch:
            // Nothing to do, target.crop already covers the full image
            break;
        case VideoMode::Zoom:
            if(zoom_factor == -1)
                pl_rect2df_aspect_copy(&target_frame.crop, &crop, 1.0);
            else
            {
                const float z = powf(2.0f, zoom_factor);
                const float sx = z * fabsf(pl_rect_w(crop)) / pl_rect_w(target_frame.crop);
                const float sy = z * fabsf(pl_rect_h(crop)) / pl_rect_h(target_frame.crop);
                pl_rect2df_stretch(&target_frame.crop, sx, sy);
            }
            break;
        }
    }

    // Disable background transparency by default if the swapchain does not
    // appear to support alpha transaprency
    if (sw_frame.color_repr.alpha == PL_ALPHA_NONE)
        params.background_transparency = 0.0;

    const struct pl_hook *fsrcnnx_hook = nullptr;
    if (frame_mix.num_frames) {
        const float src_width = fabsf(pl_rect_w(frame_mix.frames[0]->crop));
        const float src_height = fabsf(pl_rect_h(frame_mix.frames[0]->crop));
        const float dst_width = fabsf(pl_rect_w(target_frame.crop));
        const float dst_height = fabsf(pl_rect_h(target_frame.crop));
        if (src_width > 0.0f && src_height > 0.0f) {
            const float upscale_factor = qMin(dst_width / src_width, dst_height / src_height);
            if (video_preset == VideoPreset::Custom && configured_upscaler == PlaceboUpscaler::FSR) {
                if (upscale_factor > 1.0f)
                    fsrcnnx_hook = fsr_hook;
            } else if (upscale_factor >= 1.3f) {
                switch (video_preset) {
                case VideoPreset::HighQualitySpatial:
                    fsrcnnx_hook = fsrcnnx_hook_8;
                    break;
                case VideoPreset::HighQualityAdvancedSpatial:
                    fsrcnnx_hook = fsrcnnx_hook_16;
                    break;
                case VideoPreset::Custom:
                    if (configured_upscaler == PlaceboUpscaler::FSR && upscale_factor > 1.0f)
                        fsrcnnx_hook = fsr_hook;
                    else
                        fsrcnnx_hook = selected_fsrcnnx_hook(configured_upscaler, fsrcnnx_hook_8, fsrcnnx_hook_16);
                    break;
                default:
                    break;
                }
            }
        }
    }
    const struct pl_hook *active_hooks[] = {fsrcnnx_hook};
    if (fsrcnnx_hook) {
        params.hooks = active_hooks;
        params.num_hooks = 1;
        switch (video_preset) {
        case VideoPreset::HighQualitySpatial:
            qCDebug(chiakiGui) << "Activating spatial upscaler: FSRCNNX x2 8-0-4-1";
            break;
        case VideoPreset::HighQualityAdvancedSpatial:
            qCDebug(chiakiGui) << "Activating spatial upscaler: FSRCNNX x2 16-0-4-1";
            break;
        case VideoPreset::Custom:
            switch (configured_upscaler) {
            case PlaceboUpscaler::FSR:
                qCDebug(chiakiGui) << "Activating custom upscaler: FSR";
                break;
            case PlaceboUpscaler::FSRCNNX8:
                qCDebug(chiakiGui) << "Activating custom upscaler: FSRCNNX x2 8-0-4-1";
                break;
            case PlaceboUpscaler::FSRCNNX16:
                qCDebug(chiakiGui) << "Activating custom upscaler: FSRCNNX x2 16-0-4-1";
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    if (!pl_render_image_mix(placebo_renderer, &frame_mix, &target_frame, &params))
    {
        qCWarning(chiakiGui) << "Failed to render Placebo frame!";
        finalize_render();
        return;
    }

    if (!pl_swapchain_submit_frame(placebo_swapchain))
        qCWarning(chiakiGui) << "Failed to submit Placebo frame!";

    pl_swapchain_swap_buffers(placebo_swapchain);

    if (!playback_started) {
        pl_gpu_flush(placeboGpu());
        ts_start = chiaki_time_now_monotonic_us();
        playback_started = true;
    }

    schedule_next_update = has_video && queue_status != PL_QUEUE_EOF;

    finalize_render();
    if (schedule_next_update) {
        QMetaObject::invokeMethod(this, [this]() {
            scheduleUpdate();
        }, Qt::QueuedConnection);
    }

}

bool QmlMainWindow::handleShortcut(QKeyEvent *event)
{
    if (event->modifiers() == Qt::NoModifier) {
        switch (event->key()) {
        case Qt::Key_F11:
            if (windowState() != Qt::WindowFullScreen)
                fullscreenTime();
            else
                normalTime();
            return true;
        default:
            break;
        }
    }

    if (!event->modifiers().testFlag(Qt::ControlModifier))
        return false;

    switch (event->key()) {
    case Qt::Key_S:
        if (has_video)
            setVideoMode(videoMode() == VideoMode::Stretch ? VideoMode::Normal : VideoMode::Stretch);
        return true;
    case Qt::Key_Z:
        if (has_video)
            setVideoMode(videoMode() == VideoMode::Zoom ? VideoMode::Normal : VideoMode::Zoom);
        return true;
    case Qt::Key_M:
        if (session)
            session->ToggleMute();
        return true;
    case Qt::Key_O:
        emit menuRequested();
        return true;
    case Qt::Key_Q:
#ifndef Q_OS_MACOS
        close();
#endif
        return true;
    default:
        return false;
    }
}

bool QmlMainWindow::amdCard() const
{
    return amd_card;
}

bool QmlMainWindow::nvidiaCard() const
{
    return nvidia_card;
}

bool QmlMainWindow::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
        if (static_cast<QMouseEvent*>(event)->source() != Qt::MouseEventNotSynthesized)
            return true;
        if (session && !grab_input) {
            if (event->type() == QEvent::MouseMove)
                session->HandleMouseMoveEvent(static_cast<QMouseEvent*>(event), width(), height());
            else if (event->type() == QEvent::MouseButtonPress)
                session->HandleMousePressEvent(static_cast<QMouseEvent*>(event));
            else if (event->type() == QEvent::MouseButtonRelease)
                session->HandleMouseReleaseEvent(static_cast<QMouseEvent*>(event));
            return true;
        }
        QGuiApplication::sendEvent(quick_window, event);
        break;
    case QEvent::MouseButtonDblClick:
        if(!settings->GetFullscreenDoubleClickEnabled())
            break;
        if (session && !grab_input) {
            if (windowState() != Qt::WindowFullScreen)
                fullscreenTime();
            else
                normalTime();
        }
        break;
    case QEvent::KeyPress:
        if (handleShortcut(static_cast<QKeyEvent*>(event)))
            return true;
    case QEvent::KeyRelease:
        if (session && !grab_input) {
            QKeyEvent *e = static_cast<QKeyEvent*>(event);
            if (!e->spontaneous()) {
                QGuiApplication::sendEvent(quick_window, event);
                return true;
            }
            if (e->timestamp())
                session->HandleKeyboardEvent(e);
            return true;
        }
        QGuiApplication::sendEvent(quick_window, event);
        break;
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
        if (session && !grab_input) {
            session->HandleTouchEvent(static_cast<QTouchEvent*>(event), width(), height());
            return true;
        }
        QGuiApplication::sendEvent(quick_window, event);
        break;
    case QEvent::Close:
        if (!backend->closeRequested()) {
            event->ignore();
            return true;
        }
        if (quick_render->thread() == QThread::currentThread())
            destroySwapchain();
        else
            QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::destroySwapchain, this), Qt::BlockingQueuedConnection);
        break;
    default:
        break;
    }

    bool ret = QWindow::event(event);

    switch (event->type()) {
    case QEvent::Expose:
        if (isExposed())
            updateSwapchain();
        else
            if (quick_render->thread() == QThread::currentThread())
                destroySwapchain();
            else
                QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::destroySwapchain, this), Qt::BlockingQueuedConnection);
        break;
    case QEvent::Move:
        if(!session && isWindowAdjustable())
            settings->SetGeometry(geometry());
        else if(session && settings->GetWindowType() == WindowType::AdjustableResolution && isStreamWindowAdjustable())
            settings->SetStreamGeometry(geometry());
        break;
    case QEvent::Resize:
        if(!session && isWindowAdjustable())
            settings->SetGeometry(geometry());
        else if(session && settings->GetWindowType() == WindowType::AdjustableResolution && isStreamWindowAdjustable())
            settings->SetStreamGeometry(geometry());
        if (isExposed())
            updateSwapchain();
        break;
    default:
        break;
    }

    return ret;
}

QObject *QmlMainWindow::focusObject() const
{
    return quick_window->focusObject();
}
