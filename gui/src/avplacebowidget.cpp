// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <avplacebowidget.h>
#include <avplaceboframeuploader.h>
#include <streamsession.h>

#include <QThread>
#include <QTimer>
#include <QGuiApplication>
#include <QWindow>
#include <stdio.h>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QStandardPaths>

#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>

#include <xcb/xcb.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xcb.h>
#include <vulkan/vulkan_wayland.h>
#include <qpa/qplatformnativeinterface.h>

static inline QString GetShaderCacheFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pl_shader.cache";
}

static bool UploadImage(const QImage &img, pl_gpu gpu, struct pl_plane *out_plane, pl_tex *tex)
{
    struct pl_plane_data data = {
        .type = PL_FMT_UNORM,
        .width = img.width(),
        .height = img.height(),
        .pixel_stride = static_cast<size_t>(img.bytesPerLine() / img.width()),
        .pixels = img.constBits(),
    };

    for (int c = 0; c < 4; c++) {
        data.component_size[c] = img.pixelFormat().redSize();
        data.component_pad[c] = 0;
        data.component_map[c] = c;
    }

    return pl_upload_plane(gpu, out_plane, tex, &data);
}

AVPlaceboWidget::AVPlaceboWidget(StreamSession *session, ResolutionMode resolution_mode, PlaceboPreset preset)
    : session(session), resolution_mode(resolution_mode)
{
    setSurfaceType(QWindow::VulkanSurface);

    if (preset == PlaceboPreset::Default)
    {
        CHIAKI_LOGI(session->GetChiakiLog(), "Using placebo default preset");
        render_params = pl_render_default_params;
    }
    else if (preset == PlaceboPreset::Fast)
    {
        CHIAKI_LOGI(session->GetChiakiLog(), "Using placebo fast preset");
        render_params = pl_render_fast_params;
    }
    else
    {
        CHIAKI_LOGI(session->GetChiakiLog(), "Using placebo high quality preset");
        render_params = pl_render_high_quality_params;
    }

    const char *vk_exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        nullptr,
    };

    QString platformName = QGuiApplication::platformName();
    if (platformName.startsWith("wayland")) {
        vk_exts[1] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
    } else if (platformName.startsWith("xcb")) {
        vk_exts[1] = VK_KHR_XCB_SURFACE_EXTENSION_NAME;
    } else {
        Q_UNREACHABLE();
    }

    const char *opt_extensions[] = {
        VK_EXT_HDR_METADATA_EXTENSION_NAME,
    };

    struct pl_log_params log_params = {
        .log_cb = PlaceboLog,
        .log_priv = session->GetChiakiLog(),
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

    struct pl_vulkan_params vulkan_params = {
        .instance = placebo_vk_inst->instance,
        .get_proc_addr = placebo_vk_inst->get_proc_addr,
        PL_VULKAN_DEFAULTS
    };
    placebo_vulkan = pl_vulkan_create(placebo_log, &vulkan_params);

    struct pl_cache_params cache_params = {
        .log = placebo_log,
        .max_total_size = 10 << 20, // 10 MB
    };
    placebo_cache = pl_cache_create(&cache_params);
    pl_gpu_set_cache(placebo_vulkan->gpu, placebo_cache);
    FILE *file = fopen(qPrintable(GetShaderCacheFile()), "rb");
    if (file) {
        pl_cache_load_file(placebo_cache, file);
        fclose(file);
    }
}

AVPlaceboWidget::~AVPlaceboWidget()
{
    ReleaseSwapchain();

    FILE *file = fopen(qPrintable(GetShaderCacheFile()), "wb");
    if (file) {
        pl_cache_save_file(placebo_cache, file);
        fclose(file);
    }
    pl_cache_destroy(&placebo_cache);
    pl_vulkan_destroy(&placebo_vulkan);
    pl_vk_inst_destroy(&placebo_vk_inst);
    pl_log_destroy(&placebo_log);
}

void AVPlaceboWidget::showEvent(QShowEvent *event) {
    QWindow::showEvent(event);
    this->requestActivate();
}

void AVPlaceboWidget::Stop() {
    ReleaseSwapchain();
}

bool AVPlaceboWidget::ShowError(const QString &title, const QString &message) {
    error_title = title;
    error_text = message;
    RenderPlaceholderIcon();
    QTimer::singleShot(5000, this, &AVPlaceboWidget::CloseWindow);
    return true;
}

bool AVPlaceboWidget::QueueFrame(AVFrame *frame) {
    if (frame->decode_error_flags) {
        CHIAKI_LOGW(session->GetChiakiLog(), "Skip decode error!");
        av_frame_free(&frame);
        return false;
    }
    num_frames_total++;
    bool render = true;
    frames_mutex.lock();
    if (queued_frame) {
        CHIAKI_LOGV(session->GetChiakiLog(), "Dropped rendering frame!");
        num_frames_dropped++;
        av_frame_free(&queued_frame);
        render = false;
    }
    queued_frame = frame;
    frames_mutex.unlock();
    if (render) {
        QMetaObject::invokeMethod(render_thread->parent(), std::bind(&AVPlaceboWidget::RenderFrame, this));
    }
    stream_started = true;
    return true;
}

void AVPlaceboWidget::RenderFrame()
{
    struct pl_swapchain_frame sw_frame = {0};
    struct pl_frame placebo_frame = {0};
    struct pl_frame target_frame = {0};
    pl_tex overlay_tex = {0};
    struct pl_overlay_part overlay_part = {0};
    struct pl_overlay overlay = {0};

    frames_mutex.lock();
    AVFrame *frame = queued_frame;
    queued_frame = nullptr;
    frames_mutex.unlock();

    if (!frame) {
        CHIAKI_LOGE(session->GetChiakiLog(), "No frame to render!");
        return;
    }

    struct pl_avframe_params avparams = {
        .frame = frame,
        .tex = placebo_tex,
        .map_dovi = false,
    };

    bool mapped = pl_map_avframe_ex(placebo_vulkan->gpu, &placebo_frame, &avparams);
    av_frame_free(&frame);
    if (!mapped) {
        CHIAKI_LOGE(session->GetChiakiLog(), "Failed to map AVFrame to Placebo frame!");
        return;
    }

    // XXX: Revert when it's fixed in gamescope
    if (!first_frame_done) {
        first_frame_done = true;
        pl_swapchain_destroy(&placebo_swapchain);
        struct pl_vulkan_swapchain_params swapchain_params = {
            .surface = surface,
            .present_mode = VK_PRESENT_MODE_FIFO_KHR,
        };
        placebo_swapchain = pl_vulkan_create_swapchain(placebo_vulkan, &swapchain_params);
    }

    // set colorspace hint
    struct pl_color_space hint = placebo_frame.color;
    pl_swapchain_colorspace_hint(placebo_swapchain, &hint);

    pl_rect2df crop;

    bool ret = false;
    if (!pl_swapchain_start_frame(placebo_swapchain, &sw_frame)) {
        CHIAKI_LOGE(session->GetChiakiLog(), "Failed to start Placebo frame!");
        goto cleanup;
    }

    pl_frame_from_swapchain(&target_frame, &sw_frame);

    crop = placebo_frame.crop;
    switch (resolution_mode) {
        case ResolutionMode::Normal:
            pl_rect2df_aspect_copy(&target_frame.crop, &crop, 0.0);
            break;
        case ResolutionMode::Stretch:
            // Nothing to do, target.crop already covers the full image
            break;
        case ResolutionMode::Zoom:
            pl_rect2df_aspect_copy(&target_frame.crop, &crop, 1.0);
            break;
    }

    if (!overlay_img.isNull()) {
        if (!UploadImage(overlay_img, placebo_vulkan->gpu, nullptr, &overlay_tex)) {
            CHIAKI_LOGE(session->GetChiakiLog(), "Failed to upload QImage!");
            goto cleanup;
        }
        overlay_part.src = {0, 0, static_cast<float>(overlay_img.width()), static_cast<float>(overlay_img.height())};
        overlay_part.dst = overlay_part.src;
        overlay.tex = overlay_tex;
        overlay.repr = pl_color_repr_rgb;
        overlay.color = pl_color_space_srgb;
        overlay.parts = &overlay_part;
        overlay.num_parts = 1;
        target_frame.overlays = &overlay;
        target_frame.num_overlays = 1;
    }

    if (!pl_render_image(placebo_renderer, &placebo_frame, &target_frame,
                         &render_params)) {
        CHIAKI_LOGE(session->GetChiakiLog(), "Failed to render Placebo frame!");
        goto cleanup;
    }

    if (!pl_swapchain_submit_frame(placebo_swapchain)) {
        CHIAKI_LOGE(session->GetChiakiLog(), "Failed to submit Placebo frame!");
        goto cleanup;
    }
    pl_swapchain_swap_buffers(placebo_swapchain);

cleanup:
    pl_unmap_avframe(placebo_vulkan->gpu, &placebo_frame);
    pl_tex_destroy(placebo_vulkan->gpu, &overlay_tex);
}

void AVPlaceboWidget::RenderImage(const QImage &img)
{
    struct pl_frame target_frame = {0};
    struct pl_swapchain_frame sw_frame = {0};
    struct pl_plane plane = {0};
    pl_tex tex = {0};

    if (!placebo_renderer) {
        CHIAKI_LOGE(session->GetChiakiLog(), "No renderer!");
        return;
    }

    if (!pl_swapchain_start_frame(placebo_swapchain, &sw_frame)) {
        CHIAKI_LOGE(session->GetChiakiLog(), "Failed to start Placebo frame!");
        return;
    }

    pl_frame_from_swapchain(&target_frame, &sw_frame);

    if (!UploadImage(img, placebo_vulkan->gpu, &plane, &tex)) {
        CHIAKI_LOGE(session->GetChiakiLog(), "Failed to upload QImage!");
        return;
    }

    struct pl_frame image = {
        .num_planes = 1,
        .planes = { plane },
        .repr = pl_color_repr_rgb,
        .color = pl_color_space_srgb,
        .crop = {0, 0, static_cast<float>(img.width()), static_cast<float>(img.height())},
    };

    pl_render_image(placebo_renderer, &image, &target_frame, &render_params);
    pl_swapchain_submit_frame(placebo_swapchain);
    pl_swapchain_swap_buffers(placebo_swapchain);
    pl_tex_destroy(placebo_vulkan->gpu, &tex);
}

void AVPlaceboWidget::RenderPlaceholderIcon()
{
    QImage img(size() * devicePixelRatio(), QImage::Format_RGBA8888);
    img.fill(Qt::black);

    QImageReader logo_reader(":/icons/chiaki.svg");
    int logo_size = std::min(img.width(), img.height()) / 2;
    logo_reader.setScaledSize(QSize(logo_size, logo_size));
    QImage logo_img = logo_reader.read();
    QPainter p(&img);
    QRect imageRect((img.width() - logo_img.width()) / 2, (img.height() - logo_img.height()) / 2, logo_img.width(), logo_img.height());
    p.drawImage(imageRect, logo_img);
    if (!error_title.isEmpty()) {
        QFont f = p.font();
        f.setPixelSize(26 * devicePixelRatio());
        p.setPen(Qt::white);
        f.setBold(true);
        p.setFont(f);
        int title_height = QFontMetrics(f).boundingRect(error_title).height();
        int title_y = imageRect.bottom() + (img.height() - imageRect.bottom() - title_height * 5) / 2;
        p.drawText(QRect(0, title_y, img.width(), title_height), Qt::AlignCenter, error_title);
        f.setPixelSize(22 * devicePixelRatio());
        f.setBold(false);
        p.setFont(f);
        p.drawText(QRect(0, title_y + title_height + 10, img.width(), img.height()), Qt::AlignTop | Qt::AlignHCenter, error_text);
    }
    p.end();

    if (render_thread)
        QMetaObject::invokeMethod(render_thread->parent(), std::bind(&AVPlaceboWidget::RenderImage, this, img));
    else
        RenderImage(img);
}

void AVPlaceboWidget::CreateSwapchain()
{
    VkResult err;
    if (QGuiApplication::platformName().startsWith("wayland")) {
        PFN_vkCreateWaylandSurfaceKHR createSurface = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
                placebo_vk_inst->get_proc_addr(placebo_vk_inst->instance, "vkCreateWaylandSurfaceKHR"));

        VkWaylandSurfaceCreateInfoKHR surfaceInfo;
        memset(&surfaceInfo, 0, sizeof(surfaceInfo));
        surfaceInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        QPlatformNativeInterface* pni = QGuiApplication::platformNativeInterface();
        surfaceInfo.display = static_cast<struct wl_display*>(pni->nativeResourceForWindow("display", this));
        surfaceInfo.surface = static_cast<struct wl_surface*>(pni->nativeResourceForWindow("surface", this));
        err = createSurface(placebo_vk_inst->instance, &surfaceInfo, nullptr, &surface);
    } else if (QGuiApplication::platformName().startsWith("xcb")) {
        PFN_vkCreateXcbSurfaceKHR createSurface = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
                placebo_vk_inst->get_proc_addr(placebo_vk_inst->instance, "vkCreateXcbSurfaceKHR"));

        VkXcbSurfaceCreateInfoKHR surfaceInfo;
        memset(&surfaceInfo, 0, sizeof(surfaceInfo));
        surfaceInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        QPlatformNativeInterface* pni = QGuiApplication::platformNativeInterface();
        surfaceInfo.connection = static_cast<xcb_connection_t*>(pni->nativeResourceForWindow("connection", this));
        surfaceInfo.window = static_cast<xcb_window_t>(winId());
        err = createSurface(placebo_vk_inst->instance, &surfaceInfo, nullptr, &surface);
    } else {
        Q_UNREACHABLE();
    }

    if (err != VK_SUCCESS)
        qFatal("Failed to create VkSurfaceKHR");

    struct pl_vulkan_swapchain_params swapchain_params = {
        .surface = surface,
        .present_mode = VK_PRESENT_MODE_FIFO_KHR,
    };
    placebo_swapchain = pl_vulkan_create_swapchain(placebo_vulkan, &swapchain_params);

    placebo_renderer = pl_renderer_create(
        placebo_log,
        placebo_vulkan->gpu
    );

    frame_uploader = new AVPlaceboFrameUploader(session, this);
    frame_uploader_thread = new QThread(this);
    frame_uploader_thread->setObjectName("Frame Uploader");
    frame_uploader->moveToThread(frame_uploader_thread);
    frame_uploader_thread->start();

    QObject *render_obj = new QObject();
    render_thread = new QThread(render_obj);
    render_thread->setObjectName("Render");
    render_thread->start();
    render_obj->moveToThread(render_thread);
}

void AVPlaceboWidget::ReleaseSwapchain()
{
    if (!frame_uploader_thread)
        return;

    frame_uploader_thread->quit();
    frame_uploader_thread->wait();
    delete frame_uploader_thread;
    frame_uploader_thread = nullptr;

    delete frame_uploader;
    frame_uploader = nullptr;

    render_thread->quit();
    render_thread->wait();
    delete render_thread->parent();
    render_thread = nullptr;

    for (int i = 0; i < 4; i++) {
        if (placebo_tex[i])
            pl_tex_destroy(placebo_vulkan->gpu, &placebo_tex[i]);
    }

    pl_renderer_destroy(&placebo_renderer);
    pl_swapchain_destroy(&placebo_swapchain);

    PFN_vkDestroySurfaceKHR destroySurface = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
            placebo_vk_inst->get_proc_addr(placebo_vk_inst->instance, "vkDestroySurfaceKHR"));
    destroySurface(placebo_vk_inst->instance, surface, nullptr);
}

void AVPlaceboWidget::CloseWindow()
{
    QWindow *p = parent();
    while (p && !p->isTopLevel())
        p = p->parent();
    p->close();
}

void AVPlaceboWidget::resizeEvent(QResizeEvent *event)
{
    QWindow::resizeEvent(event);

    if (!isVisible())
        return;

    if (!placebo_renderer)
        CreateSwapchain();

    int width = event->size().width() * this->devicePixelRatio();
    int height = event->size().height() * this->devicePixelRatio();
    pl_swapchain_resize(placebo_swapchain, &width, &height);

    if (!stream_started)
        RenderPlaceholderIcon();
}

void AVPlaceboWidget::mousePressEvent(QMouseEvent *event)
{
    if (!error_title.isEmpty())
        QTimer::singleShot(250, this, &AVPlaceboWidget::CloseWindow);

    QWindow::mousePressEvent(event);
}

void AVPlaceboWidget::HideMouse()
{
	setCursor(Qt::BlankCursor);
}

void AVPlaceboWidget::ToggleZoom()
{
	if( resolution_mode == Zoom )
		resolution_mode = Normal;
	else
		resolution_mode = Zoom;
}

void AVPlaceboWidget::ToggleStretch()
{
	if( resolution_mode == Stretch )
		resolution_mode = Normal;
	else
		resolution_mode = Stretch;
}

void AVPlaceboWidget::PlaceboLog(void *user, pl_log_level level, const char *msg)
{
    ChiakiLog *log = (ChiakiLog*)user;
    if (!log) {
        return;
    }

    ChiakiLogLevel chiaki_level;
    switch (level)
    {
        case PL_LOG_NONE:
        case PL_LOG_TRACE:
        case PL_LOG_DEBUG:
            chiaki_level = CHIAKI_LOG_VERBOSE;
            break;
        case PL_LOG_INFO:
            chiaki_level = CHIAKI_LOG_INFO;
            break;
        case PL_LOG_WARN:
            chiaki_level = CHIAKI_LOG_WARNING;
            break;
        case PL_LOG_ERR:
        case PL_LOG_FATAL:
            chiaki_level = CHIAKI_LOG_ERROR;
            break;
    }

    chiaki_log(log, chiaki_level, "[libplacebo] %s", msg);
}
