#include "qmlmainwindow.h"
#include "qmlbackend.h"
#include "chiaki/log.h"
#include "streamsession.h"

#include <qpa/qplatformnativeinterface.h>

#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>

#include <QDebug>
#include <QThread>
#include <QShortcut>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QVulkanInstance>
#include <QQuickItem>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickRenderTarget>
#include <QQuickRenderControl>
#include <QQuickGraphicsDevice>

Q_LOGGING_CATEGORY(chiakiGui, "chiaki.gui", QtInfoMsg);

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

static const char *shader_cache_path()
{
    static QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pl_shader.cache";
    return qPrintable(path);
}

QmlMainWindow::QmlMainWindow(Settings *settings)
    : QWindow()
{
    init(settings);
}

QmlMainWindow::QmlMainWindow(const StreamSessionConnectInfo &connect_info)
    : QWindow()
{
    init(connect_info.settings);
    backend->createSession(connect_info);
}

QmlMainWindow::~QmlMainWindow()
{
    QMetaObject::invokeMethod(quick_render, [this]() {
        destroySwapchain();
        quick_render->invalidate();
    }, Qt::BlockingQueuedConnection);
    render_thread->quit();
    render_thread->wait();
    delete render_thread->parent();

    delete quick_render;
    delete quick_item;
    delete quick_window;
    delete qml_engine;
    delete qt_vk_inst;

    pl_tex_destroy(placebo_vulkan->gpu, &quick_tex);
    pl_vulkan_sem_destroy(placebo_vulkan->gpu, &quick_sem);
    for (int i = 0; i < 4; i++)
        pl_tex_destroy(placebo_vulkan->gpu, &placebo_tex[i]);

    FILE *file = fopen(shader_cache_path(), "wb");
    if (file) {
        pl_cache_save_file(placebo_cache, file);
        fclose(file);
    }
    pl_cache_destroy(&placebo_cache);
    pl_renderer_destroy(&placebo_renderer);
    pl_vulkan_destroy(&placebo_vulkan);
    pl_vk_inst_destroy(&placebo_vk_inst);
    pl_log_destroy(&placebo_log);
}

bool QmlMainWindow::hasVideo() const
{
    return has_video;
}

int QmlMainWindow::corruptedFrames() const
{
    return corrupted_frames;
}

bool QmlMainWindow::keepVideo() const
{
    return keep_video;
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
    if (!grab_input && has_video)
        setCursor(Qt::BlankCursor);
    if (session)
        session->BlockInput(grab_input);
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

QmlMainWindow::VideoPreset QmlMainWindow::videoPreset() const
{
    return video_preset;
}

void QmlMainWindow::setVideoPreset(VideoPreset preset)
{
    video_preset = preset;
    emit videoPresetChanged();
}

void QmlMainWindow::show()
{
    QQmlComponent component(qml_engine, QUrl(QStringLiteral("qrc:/Main.qml")));
    if (!component.isReady()) {
        qCCritical(chiakiGui) << "Component not ready\n" << component.errors();
        return;
    }

    QVariantMap props;
    props[QStringLiteral("parent")] = QVariant::fromValue(quick_window->contentItem());
    quick_item = qobject_cast<QQuickItem*>(component.createWithInitialProperties(props));
    if (!quick_item) {
        qCCritical(chiakiGui) << "Failed to create root item\n" << component.errors();
        return;
    }

    resize(1280, 720);

    if (qEnvironmentVariable("XDG_CURRENT_DESKTOP") == "gamescope")
        showFullScreen();
    else
        showNormal();
}

void QmlMainWindow::presentFrame(AVFrame *frame)
{
    int corrupted_now = corrupted_frames;

    frame_mutex.lock();
    if (frame->decode_error_flags) {
        corrupted_now++;
        qCDebug(chiakiGui) << "Dropping decode error frame";
        av_frame_free(&frame);
    } else if (next_frame) {
        qCDebug(chiakiGui) << "Dropping rendering frame";
        av_frame_free(&next_frame);
    }
    if (frame)
        next_frame = frame;
    frame_mutex.unlock();

    if (corrupted_now == corrupted_frames)
        corrupted_now = 0;

    if (corrupted_now != corrupted_frames) {
        corrupted_frames = corrupted_now;
        emit corruptedFramesChanged();
    }

    if (!has_video) {
        has_video = true;
        if (!grab_input)
            setCursor(Qt::BlankCursor);
        emit hasVideoChanged();
    }

    update();
}

QSurfaceFormat QmlMainWindow::createSurfaceFormat()
{
    QSurfaceFormat format;
    format.setAlphaBufferSize(8);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    return format;
}

void QmlMainWindow::init(Settings *settings)
{
    setSurfaceType(QWindow::VulkanSurface);

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
#elif defined(Q_OS_WIN32)
    vk_exts[0] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#endif

    const char *opt_extensions[] = {
        VK_EXT_HDR_METADATA_EXTENSION_NAME,
    };

    struct pl_log_params log_params = {
        .log_cb = placebo_log_cb,
        .log_level = PL_LOG_DEBUG,
    };
    placebo_log = pl_log_create(PL_API_VER, &log_params);

    struct pl_vk_inst_params vk_inst_params = {
        .extensions = vk_exts,
        .num_extensions = 2,
        .opt_extensions = opt_extensions,
        .num_opt_extensions = 1,
    };
    placebo_vk_inst = pl_vk_inst_create(placebo_log, &vk_inst_params);

#define GET_PROC(name_) { \
    vk_funcs.name_ = reinterpret_cast<decltype(vk_funcs.name_)>( \
        placebo_vk_inst->get_proc_addr(placebo_vk_inst->instance, #name_)); \
    if (!vk_funcs.name_) { \
        qCCritical(chiakiGui) << "Failed to resolve" << #name_; \
        return; \
    } }
    GET_PROC(vkGetDeviceProcAddr)
#if defined(Q_OS_LINUX)
    if (QGuiApplication::platformName().startsWith("wayland"))
        GET_PROC(vkCreateWaylandSurfaceKHR)
    else if (QGuiApplication::platformName().startsWith("xcb"))
        GET_PROC(vkCreateXcbSurfaceKHR)
#elif defined(Q_OS_MACOS)
    GET_PROC(vkCreateMetalSurfaceEXT)
#elif defined(Q_OS_WIN32)
    GET_PROC(vkCreateWin32SurfaceKHR)
#endif
    GET_PROC(vkDestroySurfaceKHR)
#undef GET_PROC

    struct pl_vulkan_params vulkan_params = {
        .instance = placebo_vk_inst->instance,
        .get_proc_addr = placebo_vk_inst->get_proc_addr,
        .allow_software = true,
        PL_VULKAN_DEFAULTS
    };
    placebo_vulkan = pl_vulkan_create(placebo_log, &vulkan_params);

#define GET_PROC(name_) { \
    vk_funcs.name_ = reinterpret_cast<decltype(vk_funcs.name_)>( \
        vk_funcs.vkGetDeviceProcAddr(placebo_vulkan->device, #name_)); \
    if (!vk_funcs.name_) { \
        qCCritical(chiakiGui) << "Failed to resolve" << #name_; \
        return; \
    } }
    GET_PROC(vkWaitSemaphores)
#undef GET_PROC

    struct pl_cache_params cache_params = {
        .log = placebo_log,
        .max_total_size = 10 << 20, // 10 MB
    };
    placebo_cache = pl_cache_create(&cache_params);
    pl_gpu_set_cache(placebo_vulkan->gpu, placebo_cache);
    FILE *file = fopen(shader_cache_path(), "rb");
    if (file) {
        pl_cache_load_file(placebo_cache, file);
        fclose(file);
    }

    placebo_renderer = pl_renderer_create(
        placebo_log,
        placebo_vulkan->gpu
    );

    struct pl_vulkan_sem_params sem_params = {
        .type = VK_SEMAPHORE_TYPE_TIMELINE,
    };
    quick_sem = pl_vulkan_sem_create(placebo_vulkan->gpu, &sem_params);

    qt_vk_inst = new QVulkanInstance;
    qt_vk_inst->setVkInstance(placebo_vk_inst->instance);
    if (!qt_vk_inst->create())
        qFatal("Failed to create QVulkanInstance");

    quick_render = new QQuickRenderControl;

    QQuickWindow::setDefaultAlphaBuffer(true);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    quick_window = new QQuickWindow(quick_render);
    quick_window->setVulkanInstance(qt_vk_inst);
    quick_window->setGraphicsDevice(QQuickGraphicsDevice::fromDeviceObjects(placebo_vulkan->phys_device, placebo_vulkan->device, placebo_vulkan->queue_graphics.index));
    quick_window->setColor(QColor(0, 0, 0, 0));
    connect(quick_window, &QQuickWindow::focusObjectChanged, this, &QmlMainWindow::focusObjectChanged);

    qml_engine = new QQmlEngine(this);
    if (!qml_engine->incubationController())
        qml_engine->setIncubationController(quick_window->incubationController());
    connect(qml_engine, &QQmlEngine::quit, this, &QWindow::close);

    backend = new QmlBackend(settings, this);
    connect(backend, &QmlBackend::sessionChanged, this, [this](StreamSession *s) {
        session = s;
        grab_input = 0;
        if (has_video) {
            has_video = false;
            setCursor(Qt::ArrowCursor);
            emit hasVideoChanged();
        }
    });

    render_thread = new QThread;
    render_thread->setObjectName("render");
    render_thread->start();

    quick_render->prepareThread(render_thread);
    quick_render->moveToThread(render_thread);

    connect(quick_render, &QQuickRenderControl::sceneChanged, this, [this]() {
        quick_need_sync = true;
        scheduleUpdate();
    });
    connect(quick_render, &QQuickRenderControl::renderRequested, this, [this]() {
        quick_need_render = true;
        scheduleUpdate();
    });

    update_timer = new QTimer(this);
    update_timer->setSingleShot(true);
    connect(update_timer, &QTimer::timeout, this, &QmlMainWindow::update);

    QMetaObject::invokeMethod(quick_render, &QQuickRenderControl::initialize);
}

void QmlMainWindow::update()
{
    Q_ASSERT(QThread::currentThread() == QGuiApplication::instance()->thread());

    if (render_scheduled)
        return;

    if (quick_need_sync) {
        quick_need_sync = false;
        quick_render->polishItems();
        QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::sync, this), Qt::BlockingQueuedConnection);
    }

    update_timer->stop();
    render_scheduled = true;
    QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::render, this));
}

void QmlMainWindow::scheduleUpdate()
{
    Q_ASSERT(QThread::currentThread() == QGuiApplication::instance()->thread());

    if (!update_timer->isActive())
        update_timer->start(has_video ? 50 : 10);
}

void QmlMainWindow::createSwapchain()
{
    Q_ASSERT(QThread::currentThread() == render_thread);

    if (placebo_swapchain)
        return;

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
#elif defined(Q_OS_WIN32)
    VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = GetModuleHandle(nullptr);
    surfaceInfo.hwnd = static_cast<HWND>(winId());
    err = vk_funcs.vkCreateWin32SurfaceKHR(placebo_vk_inst->instance, &surfaceInfo, nullptr, &surface);
#endif

    if (err != VK_SUCCESS)
        qFatal("Failed to create VkSurfaceKHR");

    struct pl_vulkan_swapchain_params swapchain_params = {
        .surface = surface,
        .present_mode = VK_PRESENT_MODE_MAILBOX_KHR,
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
    vk_funcs.vkDestroySurfaceKHR(placebo_vk_inst->instance, surface, nullptr);
}

void QmlMainWindow::resizeSwapchain()
{
    Q_ASSERT(QThread::currentThread() == render_thread);

    if (!placebo_swapchain)
        createSwapchain();

    const QSize window_size(width() * devicePixelRatio(), height() * devicePixelRatio());
    if (window_size == swapchain_size)
        return;

    swapchain_size = window_size;
    pl_swapchain_resize(placebo_swapchain, &swapchain_size.rwidth(), &swapchain_size.rheight());

    struct pl_tex_params tex_params = {
        .w = swapchain_size.width(),
        .h = swapchain_size.height(),
        .format = pl_find_fmt(placebo_vulkan->gpu, PL_FMT_UNORM, 4, 0, 0, PL_FMT_CAP_RENDERABLE),
        .sampleable = true,
        .renderable = true,
    };
    if (!pl_tex_recreate(placebo_vulkan->gpu, &quick_tex, &tex_params))
        qCCritical(chiakiGui) << "Failed to create placebo texture";

    VkFormat vk_format;
    VkImage vk_image = pl_vulkan_unwrap(placebo_vulkan->gpu, quick_tex, &vk_format, nullptr);
    quick_window->setRenderTarget(QQuickRenderTarget::fromVulkanImage(vk_image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, vk_format, swapchain_size));
}

void QmlMainWindow::updateSwapchain()
{
    Q_ASSERT(QThread::currentThread() == QGuiApplication::instance()->thread());

    quick_item->setSize(size());
    quick_window->resize(size());

    QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::resizeSwapchain, this), Qt::BlockingQueuedConnection);
    quick_render->polishItems();
    QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::sync, this), Qt::BlockingQueuedConnection);
    update();
}

void QmlMainWindow::sync()
{
    Q_ASSERT(QThread::currentThread() == render_thread);

    if (!quick_tex)
        return;

    beginFrame();
    quick_need_render = quick_render->sync();
}

void QmlMainWindow::beginFrame()
{
    if (quick_frame)
        return;

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

    render_scheduled = false;

    if (!placebo_swapchain)
        return;

    frame_mutex.lock();
    if (next_frame || (!has_video && !keep_video)) {
        av_frame_free(&current_frame);
        std::swap(current_frame, next_frame);
    }
    frame_mutex.unlock();

    struct pl_swapchain_frame sw_frame = {};
    if (!pl_swapchain_start_frame(placebo_swapchain, &sw_frame)) {
        qCWarning(chiakiGui) << "Failed to start Placebo frame!";
        return;
    }

    struct pl_frame target_frame = {};
    pl_frame_from_swapchain(&target_frame, &sw_frame);

    if (quick_need_render) {
        quick_need_render = false;
        beginFrame();
        quick_render->render();
    }
    endFrame();

    struct pl_overlay_part overlay_part = {
        .src = {0, 0, static_cast<float>(swapchain_size.width()), static_cast<float>(swapchain_size.height())},
        .dst = {0, 0, static_cast<float>(swapchain_size.width()), static_cast<float>(swapchain_size.height())},
    };
    struct pl_overlay overlay = {
        .tex = quick_tex,
        .repr = pl_color_repr_rgb,
        .color = pl_color_space_srgb,
        .parts = &overlay_part,
        .num_parts = 1,
    };
    target_frame.overlays = &overlay;
    target_frame.num_overlays = 1;

    struct pl_frame placebo_frame = {};
    if (current_frame) {
        struct pl_avframe_params avparams = {
            .frame = current_frame,
            .tex = placebo_tex,
        };
        if (!pl_map_avframe_ex(placebo_vulkan->gpu, &placebo_frame, &avparams))
            qCWarning(chiakiGui) << "Failed to map AVFrame to Placebo frame!";

        pl_rect2df crop = placebo_frame.crop;
        switch (video_mode) {
        case VideoMode::Normal:
            pl_rect2df_aspect_copy(&target_frame.crop, &crop, 0.0);
            break;
        case VideoMode::Stretch:
            // Nothing to do, target.crop already covers the full image
            break;
        case VideoMode::Zoom:
            pl_rect2df_aspect_copy(&target_frame.crop, &crop, 1.0);
            break;
        }
        pl_swapchain_colorspace_hint(placebo_swapchain, &placebo_frame.color);
    }

    const struct pl_render_params *render_params;
    switch (video_preset) {
    case VideoPreset::Fast:
        render_params = &pl_render_fast_params;
        break;
    case VideoPreset::Default:
        render_params = &pl_render_default_params;
        break;
    case VideoPreset::HighQuality:
        render_params = &pl_render_high_quality_params;
        break;
    }
    if (!pl_render_image(placebo_renderer, current_frame ? &placebo_frame : nullptr, &target_frame, render_params))
        qCWarning(chiakiGui) << "Failed to render Placebo frame!";

    if (!pl_swapchain_submit_frame(placebo_swapchain))
        qCWarning(chiakiGui) << "Failed to submit Placebo frame!";

    pl_swapchain_swap_buffers(placebo_swapchain);

    pl_unmap_avframe(placebo_vulkan->gpu, &placebo_frame);
}

bool QmlMainWindow::handleShortcut(QKeyEvent *event)
{
    if (!event->modifiers().testFlag(Qt::ControlModifier))
        return false;

    switch (event->key()) {
    case Qt::Key_F11:
        if (windowState() != Qt::WindowFullScreen)
            showFullScreen();
        else
            showNormal();
        return true;
    case Qt::Key_S:
        if (has_video)
            video_mode = video_mode == VideoMode::Stretch ? VideoMode::Normal : VideoMode::Stretch;
        return true;
    case Qt::Key_Z:
        if (has_video)
            video_mode = video_mode == VideoMode::Zoom ? VideoMode::Normal : VideoMode::Zoom;
        return true;
    case Qt::Key_M:
        if (session)
            session->ToggleMute();
        return true;
    case Qt::Key_O:
        emit menuRequested();
        return true;
    case Qt::Key_Q:
        close();
        return true;
    default:
        return false;
    }
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
    case QEvent::KeyPress:
        if (handleShortcut(static_cast<QKeyEvent*>(event)))
            return true;
    case QEvent::KeyRelease:
        if (session && !grab_input) {
            QKeyEvent *e = static_cast<QKeyEvent*>(event);
            if (e->timestamp()) // Don't send controller emulated key events
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
        if (!backend->closeRequested())
            return false;
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
            QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::destroySwapchain, this), Qt::BlockingQueuedConnection);
        break;
    case QEvent::Resize:
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
