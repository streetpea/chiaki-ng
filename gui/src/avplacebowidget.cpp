// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <avplacebowidget.h>
#include <avplaceboframeuploader.h>
#include <streamsession.h>

#include <QThread>
#include <QTimer>
#include <QGuiApplication>
#include <QWindow>
#include <stdio.h>

#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>

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

}

AVPlaceboWidget::~AVPlaceboWidget()
{
    for (int i = 0; i < 4; i++) {
        if (placebo_tex[i])
            pl_tex_destroy(placebo_vulkan->gpu, &placebo_tex[i]);
    }

    if (placebo_renderer)
        pl_renderer_destroy(&placebo_renderer);
    if (placebo_swapchain)
        pl_swapchain_destroy(&placebo_swapchain);
    if (placebo_vulkan)
        pl_vulkan_destroy(&placebo_vulkan);
    if (vulkan_instance)
        vulkan_instance->destroy();
    if (placebo_vk_inst)
        pl_vk_inst_destroy(&placebo_vk_inst);
    if (placebo_log)
        pl_log_destroy(&placebo_log);
}

void AVPlaceboWidget::showEvent(QShowEvent *event) {
    QWindow::showEvent(event);
    char** vk_exts = new char*[2]{
        (char*)VK_KHR_SURFACE_EXTENSION_NAME,
        nullptr,
    };

    QString platformName = QGuiApplication::platformName();
    if (platformName == "wayland") {
        vk_exts[1] = (char*)"VK_KHR_wayland_surface";
    } else if (platformName.contains("xcb")) {
        vk_exts[1] = (char*)"VK_KHR_xcb_surface";
    } else {
		// TODO: Bail out?
	}

	const char* opt_extensions[] = {
		VK_EXT_HDR_METADATA_EXTENSION_NAME,
	};

    // NOTE: Can't use the nice libplacebo macros for populating, since they're
    // incompatible with C++ :-/
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

    vulkan_instance = new QVulkanInstance();
    vulkan_instance->setVkInstance(placebo_vk_inst->instance);
    vulkan_instance->create();
    this->setVulkanInstance(vulkan_instance);

    VkSurfaceKHR surface = vulkan_instance->surfaceForWindow(this);

    struct pl_vulkan_params vulkan_params = {
        .instance = placebo_vk_inst->instance,
        .get_proc_addr = placebo_vk_inst->get_proc_addr,
        .surface = surface,
        .allow_software = true,
        PL_VULKAN_DEFAULTS
    };
    placebo_vulkan = pl_vulkan_create(placebo_log, &vulkan_params);

    struct pl_vulkan_swapchain_params swapchain_params = {
        .surface = surface,
        .present_mode = VK_PRESENT_MODE_FIFO_KHR,
    };
    placebo_swapchain = pl_vulkan_create_swapchain(placebo_vulkan, &swapchain_params);

    int initialWidth = this->width() * this->devicePixelRatio();
    int initialHeight = this->height() * this->devicePixelRatio();
    pl_swapchain_resize(placebo_swapchain, &initialWidth, &initialHeight);

    placebo_renderer = pl_renderer_create(
        placebo_log,
        placebo_vulkan->gpu
    );

    frame_uploader = new AVPlaceboFrameUploader(session, this);
    frame_uploader_thread = new QThread(this);
    frame_uploader_thread->setObjectName("Frame Uploader");
    frame_uploader->moveToThread(frame_uploader_thread);
    frame_uploader_thread->start();
}

void AVPlaceboWidget::Stop() {
    // Need to stop the frame uploader thread before the widget is destroyed,
    // otherwise it segfaults inside the QtWaylandClient
    // FIXME: Sometimes it still crashes on exit deep inside of Qt, why is this?
    //        Test on Steam Deck/gamescope if it happens there as well...
    if (frame_uploader_thread) {
        frame_uploader_thread->quit();
        frame_uploader_thread->wait();
        delete frame_uploader_thread;
        frame_uploader_thread = nullptr;
    }
    delete frame_uploader;
    frame_uploader = nullptr;
}

bool AVPlaceboWidget::RenderFrame(AVFrame *frame) {
    struct pl_swapchain_frame sw_frame = {0};
    struct pl_frame placebo_frame = {0};
    struct pl_frame target_frame = {0};

    struct pl_avframe_params avparams = {
        .frame = frame,
        .tex = placebo_tex,
        .map_dovi = false,
    };

    if (!pl_map_avframe_ex(placebo_vulkan->gpu, &placebo_frame, &avparams)) {
        CHIAKI_LOGE(session->GetChiakiLog(), "Failed to map AVFrame to Placebo frame!");
        return false;
    }

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
    ret = true;

cleanup:
    pl_unmap_avframe(placebo_vulkan->gpu, &placebo_frame);
    return ret;
}

void AVPlaceboWidget::resizeEvent(QResizeEvent *event)
{
    QWindow::resizeEvent(event);

    int width = event->size().width() * this->devicePixelRatio();
    int height = event->size().height() * this->devicePixelRatio();
    pl_swapchain_resize(placebo_swapchain, &width, &height);
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