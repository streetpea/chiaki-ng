// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_AVPLACEBOWIDGET_H
#define CHIAKI_AVPLACEBOWIDGET_H

#include <chiaki/log.h>

#include "avwidget.h"
#include "settings.h"

#include <QWidget>
#include <QMutex>
#include <QWindow>
#include <QElapsedTimer>
#include <libplacebo/options.h>
#include <libplacebo/vulkan.h>
#include <libplacebo/renderer.h>
#include <libplacebo/utils/frame_queue.h>

extern "C"
{
    #include <libavcodec/avcodec.h>
}

class StreamSession;
class AVPlaceboFrameUploader;


class AVPlaceboWidget : public QWindow, public IAVWidget
{
    Q_OBJECT

    private:
        StreamSession *session;
        AVPlaceboFrameUploader *frame_uploader;
        QMutex frames_mutex;
        QThread *frame_uploader_thread;
        QThread *render_thread;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        AVFrame *queued_frame = nullptr;
        bool stream_started = false;
        QImage logo_image;

        pl_render_params render_params;
        pl_log placebo_log;
        pl_vk_inst placebo_vk_inst;
        pl_vulkan placebo_vulkan;
        pl_swapchain placebo_swapchain;
        pl_renderer placebo_renderer = nullptr;
        pl_tex placebo_tex[4] = {nullptr, nullptr, nullptr, nullptr};

        qint64 num_frames_total = 0;
        qint64 num_frames_dropped = 0;

    public:
        explicit AVPlaceboWidget(
            StreamSession *session, pl_log placebo_log, pl_vk_inst placebo_vk_inst,
            ResolutionMode resolution_mode = Normal, PlaceboPreset preset = PlaceboPreset::Default
        );
        ~AVPlaceboWidget() override;

        bool QueueFrame(AVFrame *frame);
        void RenderFrame();
        void RenderImage(const QImage &img);
        void RenderPlaceholderIcon();
        void CreateSwapchain();
        void setPlaceboVulkan(pl_vulkan vulkan) { placebo_vulkan = vulkan; };
        void Stop() override;
        void showEvent(QShowEvent *event) override;
        void resizeEvent(QResizeEvent *event) override;
        static void PlaceboLog(void *user, pl_log_level level, const char *msg);
        VkSurfaceKHR vkSurface() const { return surface; }

    protected:
        ResolutionMode resolution_mode;

    public slots:
        void HideMouse();
        void ToggleStretch() override;
        void ToggleZoom() override;
};

#endif // CHIAKI_AVPLACEBOWIDGET_H
