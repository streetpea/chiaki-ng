// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_AVPLACEBOWIDGET_H
#define CHIAKI_AVPLACEBOWIDGET_H

#include <chiaki/log.h>

#include "avwidget.h"
#include "settings.h"

#include <QWidget>
#include <QMutex>
#include <QVulkanInstance>
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

        QVulkanInstance *vulkan_instance;
        pl_render_params render_params;
        pl_log placebo_log;
        pl_vk_inst placebo_vk_inst;
        pl_vulkan placebo_vulkan;
        pl_swapchain placebo_swapchain;
        pl_renderer placebo_renderer;
        #ifdef USE_FRAME_MIXING
        pl_queue frame_queue;
        pl_frame_mix mix;
        pl_options placebo_options;

        QElapsedTimer pts_timer;
        uint64_t pts_timer_start = 0;
        double pts_target = 0.0;
        double prev_pts = 0.0;
        double first_pts = 0.0;
        double base_pts = 0.0;
        double last_pts = 0.0;
        uint64_t num_frames = 0;
        #else
        pl_tex placebo_tex[4] = {nullptr, nullptr, nullptr, nullptr};
        #endif

        static void PlaceboLog(void *user, pl_log_level level, const char *msg);

        #ifdef USE_FRAME_MIXING
        static bool MapFrame(pl_gpu  gpu, pl_tex *tex, const struct pl_source_frame *src, struct pl_frame *out_frame);
        static void UnmapFrame(pl_gpu gpu, struct pl_frame *frame, const struct pl_source_frame *src);
        static void DiscardFrame(const struct pl_source_frame *src);
        #endif

    public:
        explicit AVPlaceboWidget(StreamSession *session, ResolutionMode resolution_mode = Normal, PlaceboPreset preset = PlaceboPreset::Default);
        ~AVPlaceboWidget() override;

        #ifdef USE_FRAME_MIXING
        void QueueFrame(AVFrame *frame);
        bool event(QEvent *event) override;
        #endif
        bool RenderFrame(AVFrame *frame);
        void Stop() override;
        void showEvent(QShowEvent *event) override;
        void resizeEvent(QResizeEvent *event) override;

    protected:
        ResolutionMode resolution_mode;

    public slots:
        void ToggleStretch() override;
        void ToggleZoom() override;
};

#endif // CHIAKI_AVPLACEBOWIDGET_H