#include "qmlmainwindow.h"
#include "qmlbackend.h"
#include "qmlsvgprovider.h"
#include "chiaki/log.h"
#include "chiaki/time.h"
#include "streamsession.h"

#include <qpa/qplatformnativeinterface.h>

#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
}

#include <QByteArray>
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
#include <QTimer>
#include <QQueue>
#include <QWaitCondition>
#include <QtGlobal>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <utility>
#include <atomic>
#if defined(Q_OS_MACOS)
#include <objc/message.h>
#endif

Q_LOGGING_CATEGORY(chiakiGui, "chiaki.gui", QtInfoMsg);

class DeferredPresentPacerThread final : public QThread
{
public:
    explicit DeferredPresentPacerThread(QmlMainWindow *owner) : owner(owner) {}

    void arm(qint64 target_wakeup_us)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping)
            return;
        if (!armed || target_wakeup_us < wakeup_target_us)
            wakeup_target_us = target_wakeup_us;
        armed = true;
        ++generation;
        condition.notify_one();
    }

    void disarm()
    {
        std::lock_guard<std::mutex> lock(mutex);
        armed = false;
        ++generation;
        condition.notify_one();
    }

    void stopThread()
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        armed = false;
        ++generation;
        condition.notify_one();
    }

protected:
    void run() override
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!stopping) {
            if (!armed) {
                condition.wait(lock);
                continue;
            }

            const qint64 target_wakeup_us_now = wakeup_target_us;
            const quint64 observed_generation = generation;
            const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (target_wakeup_us_now <= now_us) {
                armed = false;
                lock.unlock();
                notifyWake(static_cast<qint64>(chiaki_time_now_monotonic_us()));
                lock.lock();
                continue;
            }

            const qint64 wait_us = target_wakeup_us_now - now_us;
            condition.wait_for(lock, std::chrono::microseconds(wait_us),
                [this, observed_generation, target_wakeup_us_now]() {
                    return stopping || !armed || generation != observed_generation || wakeup_target_us != target_wakeup_us_now;
                });
            if (stopping)
                break;
            if (!armed || generation != observed_generation || wakeup_target_us != target_wakeup_us_now)
                continue;

            armed = false;
            lock.unlock();
            notifyWake(static_cast<qint64>(chiaki_time_now_monotonic_us()));
            lock.lock();
        }
    }

private:
    void notifyWake(qint64 timer_fire_us)
    {
        if (!owner)
            return;
        QMetaObject::invokeMethod(owner, [owner = this->owner, timer_fire_us]() {
            if (owner)
                owner->handleDeferredPresentWake(timer_fire_us);
        }, Qt::QueuedConnection);
    }

    QmlMainWindow *owner = nullptr;
    std::mutex mutex;
    std::condition_variable condition;
    bool stopping = false;
    bool armed = false;
    qint64 wakeup_target_us = 0;
    quint64 generation = 0;
};

class BufferedPlaybackPacerThread final : public QThread
{
public:
    explicit BufferedPlaybackPacerThread(QmlMainWindow *owner) : owner(owner) {}

    void arm(qint64 target_wakeup_us)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping)
            return;
        if (!armed || target_wakeup_us < wakeup_target_us)
            wakeup_target_us = target_wakeup_us;
        armed = true;
        ++generation;
        condition.notify_one();
    }

    void disarm()
    {
        std::lock_guard<std::mutex> lock(mutex);
        armed = false;
        ++generation;
        condition.notify_one();
    }

    void stopThread()
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        armed = false;
        ++generation;
        condition.notify_one();
    }

    bool isArmed() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return armed && !stopping;
    }

protected:
    void run() override
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!stopping) {
            if (!armed) {
                condition.wait(lock);
                continue;
            }

            const qint64 target_wakeup_us_now = wakeup_target_us;
            const quint64 observed_generation = generation;
            const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (target_wakeup_us_now <= now_us) {
                armed = false;
                lock.unlock();
                notifyWake(static_cast<qint64>(chiaki_time_now_monotonic_us()));
                lock.lock();
                continue;
            }

            const qint64 wait_us = target_wakeup_us_now - now_us;
            condition.wait_for(lock, std::chrono::microseconds(wait_us),
                [this, observed_generation, target_wakeup_us_now]() {
                    return stopping || !armed || generation != observed_generation || wakeup_target_us != target_wakeup_us_now;
                });
            if (stopping)
                break;
            if (!armed || generation != observed_generation || wakeup_target_us != target_wakeup_us_now)
                continue;

            armed = false;
            lock.unlock();
            notifyWake(static_cast<qint64>(chiaki_time_now_monotonic_us()));
            lock.lock();
        }
    }

private:
    void notifyWake(qint64 timer_fire_us)
    {
        if (!owner)
            return;
        QMetaObject::invokeMethod(owner, [owner = this->owner, timer_fire_us]() {
            if (owner)
                owner->handleBufferedPlaybackWake(timer_fire_us);
        }, Qt::QueuedConnection);
    }

    QmlMainWindow *owner = nullptr;
    mutable std::mutex mutex;
    std::condition_variable condition;
    bool stopping = false;
    bool armed = false;
    qint64 wakeup_target_us = 0;
    quint64 generation = 0;
};

struct DeferredSwapTask
{
    qint64 submit_begin_us = 0;
    qint64 submit_us = 0;
    int queue_depth_at_submit = 0;
    int depth_limit = 0;
    bool pending_frame_waiting = false;
    qint64 present_interval_us = 0;
    bool clamp_debug_enabled = false;
};

class DeferredSwapThread final : public QThread
{
public:
    explicit DeferredSwapThread(QmlMainWindow *owner)
        : owner(owner)
    {
    }

    bool enqueue(const DeferredSwapTask &task)
    {
        QMutexLocker locker(&mutex);
        if (stopping)
            return false;

        if (queue.size() >= maxQueueDepth)
            queue.dequeue();

        queue.enqueue(task);
        cond.wakeOne();
        return true;
    }

    void drain()
    {
        QMutexLocker locker(&mutex);
        while (!queue.isEmpty() || active)
            drained.wait(&mutex);
    }

    void stopThread()
    {
        QMutexLocker locker(&mutex);
        stopping = true;
        cond.wakeAll();
    }

protected:
    void run() override
    {
        for (;;) {
            DeferredSwapTask task;
            {
                QMutexLocker locker(&mutex);
                while (!stopping && queue.isEmpty())
                    cond.wait(&mutex);
                if (stopping)
                    break;
                task = queue.dequeue();
                active = true;
            }

            if (owner) {
                owner->processDeferredSwapTask(task.submit_begin_us,
                                               task.submit_us,
                                               task.queue_depth_at_submit,
                                               task.depth_limit,
                                               task.pending_frame_waiting,
                                               task.present_interval_us,
                                               task.clamp_debug_enabled);
            }

            {
                QMutexLocker locker(&mutex);
                active = false;
                if (queue.isEmpty())
                    drained.wakeAll();
            }
        }

        QMutexLocker locker(&mutex);
        active = false;
        drained.wakeAll();
    }

private:
    QmlMainWindow *owner = nullptr;
    QMutex mutex;
    QWaitCondition cond;
    QWaitCondition drained;
    QQueue<DeferredSwapTask> queue;
    bool stopping = false;
    bool active = false;
    static constexpr int maxQueueDepth = 2;
};

static void logPtsRollback(const char *source, double frame_pts, double queue_pts_origin, bool preserve_timeline, double source_frame_interval_ms)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    if (queue_pts_origin < 0.0)
        return;
    qCInfo(chiakiGui).nospace()
        << "[rollback] " << source
        << " frame_pts=" << frame_pts
        << " queue_pts_origin=" << queue_pts_origin
        << " preserve_timeline=" << preserve_timeline
        << " source_frame_interval_ms=" << source_frame_interval_ms;
}

static void logTimerFireInterval(qint64 now_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_fire_us = 0;
    static qint64 last_log_us = 0;
    static qint64 min_interval_us = 0;
    static qint64 max_interval_us = 0;
    static qint64 sum_interval_us = 0;
    static int samples = 0;

    if (last_fire_us != 0) {
        const qint64 interval_us = now_us - last_fire_us;
        if (min_interval_us == 0 || interval_us < min_interval_us)
            min_interval_us = interval_us;
        if (interval_us > max_interval_us)
            max_interval_us = interval_us;
        sum_interval_us += interval_us;
        samples++;
    }
    last_fire_us = now_us;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[timer] interval_us min=" << min_interval_us
            << " max=" << max_interval_us
            << " avg=" << (sum_interval_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_interval_us = 0;
        max_interval_us = 0;
        sum_interval_us = 0;
        samples = 0;
    }
}

static void logSwapInterval(qint64 now_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_swap_us = 0;
    static qint64 last_log_us = 0;
    static qint64 min_interval_us = 0;
    static qint64 max_interval_us = 0;
    static qint64 sum_interval_us = 0;
    static int samples = 0;

    if (last_swap_us != 0) {
        const qint64 interval_us = now_us - last_swap_us;
        if (min_interval_us == 0 || interval_us < min_interval_us)
            min_interval_us = interval_us;
        if (interval_us > max_interval_us)
            max_interval_us = interval_us;
        sum_interval_us += interval_us;
        samples++;
    }
    last_swap_us = now_us;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[swap] interval_us min=" << min_interval_us
            << " max=" << max_interval_us
            << " avg=" << (sum_interval_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_interval_us = 0;
        max_interval_us = 0;
        sum_interval_us = 0;
        samples = 0;
    }
}

static void logPresentOutlier(const char *stage,
                              qint64 now_us,
                              qint64 duration_us,
                              bool deferred_present,
                              bool present_vsync_enabled,
                              qint64 present_interval_us,
                              int queue_depth,
                              int depth_limit,
                              bool pending_frame_waiting,
                              bool render_active_now,
                              bool render_scheduled_now,
                              bool render_pending_now)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    qint64 threshold_us = 0;
    if (qstrcmp(stage, "present_interval") == 0)
        threshold_us = qMax<qint64>(45000, present_interval_us + 4000);
    else if (qstrcmp(stage, "start_frame_block") == 0)
        threshold_us = 12000;
    else if (qstrcmp(stage, "submit_call") == 0)
        threshold_us = 8000;
    else if (qstrcmp(stage, "swap_call") == 0)
        threshold_us = 8000;
    else
        threshold_us = 12000;

    if (duration_us < threshold_us)
        return;

    qCInfo(chiakiGui).nospace()
        << "[outlier] stage=" << stage
        << " duration_us=" << duration_us
        << " threshold_us=" << threshold_us
        << " pacing_mode=" << (deferred_present ? "deferred" : "immediate")
        << " swapchain_mode=" << (present_vsync_enabled ? "FIFO" : "IMMEDIATE")
        << " target_interval_us=" << present_interval_us
        << " queue_depth=" << queue_depth
        << " depth_limit=" << depth_limit
        << " pending_frame_waiting=" << (pending_frame_waiting ? 1 : 0)
        << " render_active=" << (render_active_now ? 1 : 0)
        << " render_scheduled=" << (render_scheduled_now ? 1 : 0)
        << " render_pending=" << (render_pending_now ? 1 : 0)
        << " ts_us=" << now_us;
}

static void logDeferredPresentRetry(qint64 now_us, bool render_active_now, bool render_scheduled_now)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static int retries = 0;

    retries++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000) {
        qCInfo(chiakiGui).nospace()
            << "[outlier] stage=deferred_present_retry"
            << " retries=" << retries
            << " render_active=" << (render_active_now ? 1 : 0)
            << " render_scheduled=" << (render_scheduled_now ? 1 : 0)
            << " ts_us=" << now_us;
        last_log_us = now_us;
        retries = 0;
    }
}

static void logDeferredPresentDecision(qint64 now_us,
                                       qint64 target_us,
                                       qint64 target_wakeup_us,
                                       qint64 remaining_to_target_us,
                                       qint64 start_frame_block_estimate_us,
                                       qint64 lead_us,
                                       qint64 extra_lead_us,
                                       bool deferred)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_remaining_us = 0;
    static qint64 max_remaining_us = 0;
    static qint64 min_estimate_us = 0;
    static qint64 max_estimate_us = 0;
    static qint64 min_late_budget_us = 0;
    static qint64 max_late_budget_us = 0;
    static qint64 sum_remaining_us = 0;
    static qint64 sum_estimate_us = 0;
    static int deferred_samples = 0;
    static int immediate_samples = 0;
    static int near_deadline_samples = 0;
    static int samples = 0;

    const qint64 late_budget_us = remaining_to_target_us - qMax<qint64>(0, start_frame_block_estimate_us);

    if (min_remaining_us == 0 || remaining_to_target_us < min_remaining_us)
        min_remaining_us = remaining_to_target_us;
    if (remaining_to_target_us > max_remaining_us)
        max_remaining_us = remaining_to_target_us;
    if (min_estimate_us == 0 || start_frame_block_estimate_us < min_estimate_us)
        min_estimate_us = start_frame_block_estimate_us;
    if (start_frame_block_estimate_us > max_estimate_us)
        max_estimate_us = start_frame_block_estimate_us;
    if (min_late_budget_us == 0 || late_budget_us < min_late_budget_us)
        min_late_budget_us = late_budget_us;
    if (late_budget_us > max_late_budget_us)
        max_late_budget_us = late_budget_us;

    sum_remaining_us += remaining_to_target_us;
    sum_estimate_us += start_frame_block_estimate_us;
    if (deferred)
        deferred_samples++;
    else
        immediate_samples++;
    if (late_budget_us <= 4000)
        near_deadline_samples++;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[defer] decision"
            << " deferred_samples=" << deferred_samples
            << " immediate_samples=" << immediate_samples
            << " near_deadline_samples=" << near_deadline_samples
            << " remaining_us_min=" << min_remaining_us
            << " remaining_us_max=" << max_remaining_us
            << " remaining_us_avg=" << (sum_remaining_us / samples)
            << " estimate_us_min=" << min_estimate_us
            << " estimate_us_max=" << max_estimate_us
            << " estimate_us_avg=" << (sum_estimate_us / samples)
            << " budget_us_min=" << min_late_budget_us
            << " budget_us_max=" << max_late_budget_us
            << " last_target_us=" << target_us
            << " last_wakeup_target_us=" << target_wakeup_us
            << " last_lead_us=" << lead_us
            << " extra_lead_us=" << extra_lead_us;
        last_log_us = now_us;
        min_remaining_us = 0;
        max_remaining_us = 0;
        min_estimate_us = 0;
        max_estimate_us = 0;
        min_late_budget_us = 0;
        max_late_budget_us = 0;
        sum_remaining_us = 0;
        sum_estimate_us = 0;
        deferred_samples = 0;
        immediate_samples = 0;
        near_deadline_samples = 0;
        samples = 0;
    }
}

// Estimated submit+swap cost (microseconds) used to compensate throttle sleeps.
static std::atomic<long long> g_submit_swap_est_us{1500LL};
static const long long g_submit_swap_spin_margin_us = 1500LL;

static void logDeferredTimerWakeup(qint64 timer_fire_us,
                                   qint64 target_wakeup_us,
                                   qint64 target_us,
                                   qint64 extra_lead_us,
                                   bool render_active_now,
                                   bool render_scheduled_now)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_late_us = 0;
    static qint64 max_late_us = 0;
    static qint64 sum_late_us = 0;
    static qint64 max_target_slack_us = 0;
    static int blocked_samples = 0;
    static int samples = 0;

    const qint64 late_us = target_wakeup_us > 0 ? qMax<qint64>(0, timer_fire_us - target_wakeup_us) : -1;
    const qint64 target_slack_us = target_us > 0 ? (target_us - timer_fire_us) : -1;

    if (late_us >= 0) {
        if (min_late_us == 0 || late_us < min_late_us)
            min_late_us = late_us;
        if (late_us > max_late_us)
            max_late_us = late_us;
        sum_late_us += late_us;
    }
    if (target_slack_us > max_target_slack_us)
        max_target_slack_us = target_slack_us;
    if (render_active_now || render_scheduled_now)
        blocked_samples++;
    samples++;

    if (last_log_us == 0)
        last_log_us = timer_fire_us;
    else if (timer_fire_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[defer] timer_wakeup"
            << " late_us_min=" << min_late_us
            << " late_us_max=" << max_late_us
            << " late_us_avg=" << (samples > 0 ? (sum_late_us / samples) : 0)
            << " blocked_samples=" << blocked_samples
            << " extra_lead_us=" << extra_lead_us
            << " target_slack_us_last=" << target_slack_us
            << " target_slack_us_max=" << max_target_slack_us
            << " target_wakeup_us_last=" << target_wakeup_us
            << " target_us_last=" << target_us;
        last_log_us = timer_fire_us;
        min_late_us = 0;
        max_late_us = 0;
        sum_late_us = 0;
        max_target_slack_us = 0;
        blocked_samples = 0;
        samples = 0;
    }
}

static void logBufferedPacingStats(qint64 now_us, qint64 interval_us, qint64 delta_us, bool timer_owned_playback, bool pace_timer_active)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_delta_us = 0;
    static qint64 max_delta_us = 0;
    static qint64 sum_delta_us = 0;
    static qint64 max_late_us = 0;
    static qint64 max_missed_intervals = 0;
    static int samples = 0;
    static int late_samples = 0;

    if (!timer_owned_playback)
        return;

    if (min_delta_us == 0 || delta_us < min_delta_us)
        min_delta_us = delta_us;
    if (delta_us > max_delta_us)
        max_delta_us = delta_us;
    if (delta_us < 0) {
        const qint64 late_us = -delta_us;
        if (late_us > max_late_us)
            max_late_us = late_us;
        const qint64 missed_intervals = interval_us > 0 ? (late_us / interval_us) + 1 : 1;
        if (missed_intervals > max_missed_intervals)
            max_missed_intervals = missed_intervals;
        late_samples++;
    }
    sum_delta_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[pace] timer_owned interval_us=" << interval_us
            << " delta_us_min=" << min_delta_us
            << " delta_us_max=" << max_delta_us
            << " delta_us_avg=" << (sum_delta_us / samples)
            << " late_samples=" << late_samples
            << " max_late_us=" << max_late_us
            << " max_missed_intervals=" << max_missed_intervals
            << " pace_timer_active=" << pace_timer_active;
        last_log_us = now_us;
        min_delta_us = 0;
        max_delta_us = 0;
        sum_delta_us = 0;
        max_late_us = 0;
        max_missed_intervals = 0;
        samples = 0;
        late_samples = 0;
    }
}

static void logPresentSubmitStats(qint64 now_us, bool present, bool timer_owned_playback, qint64 interval_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_sample_us = 0;
    static qint64 last_log_us = 0;
    static qint64 min_interval_us = 0;
    static qint64 max_interval_us = 0;
    static qint64 sum_interval_us = 0;
    static int samples = 0;
    static int present_samples = 0;

    if (!present)
        return;

    if (last_sample_us != 0) {
        const qint64 interval = now_us - last_sample_us;
        if (min_interval_us == 0 || interval < min_interval_us)
            min_interval_us = interval;
        if (interval > max_interval_us)
            max_interval_us = interval;
        sum_interval_us += interval;
        samples++;
    }
    last_sample_us = now_us;
    present_samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[present] submit interval_us_min=" << min_interval_us
            << " interval_us_max=" << max_interval_us
            << " interval_us_avg=" << (sum_interval_us / samples)
            << " submits=" << samples
            << " present_samples=" << present_samples
            << " timer_owned_playback=" << timer_owned_playback
            << " target_interval_us=" << interval_us;
        last_log_us = now_us;
        min_interval_us = 0;
        max_interval_us = 0;
        sum_interval_us = 0;
        samples = 0;
        present_samples = 0;
    }
}

static bool shouldUseRefreshPacedBuffering(bool stream_active, bool present_vsync_enabled, int schedule_frame_mixer_active)
{
    return stream_active && present_vsync_enabled && schedule_frame_mixer_active != 0;
}

static void logLatencyStats(const char *label, qint64 delta_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us_schedule_to_render = 0;
    static qint64 min_us_schedule_to_render = 0;
    static qint64 max_us_schedule_to_render = 0;
    static qint64 sum_us_schedule_to_render = 0;
    static int samples_schedule_to_render = 0;

    static qint64 last_log_us_render_to_swap = 0;
    static qint64 min_us_render_to_swap = 0;
    static qint64 max_us_render_to_swap = 0;
    static qint64 sum_us_render_to_swap = 0;
    static int samples_render_to_swap = 0;

    qint64 *last_log_us = nullptr;
    qint64 *min_us = nullptr;
    qint64 *max_us = nullptr;
    qint64 *sum_us = nullptr;
    int *samples = nullptr;

    if (qstrcmp(label, "schedule_to_render") == 0) {
        last_log_us = &last_log_us_schedule_to_render;
        min_us = &min_us_schedule_to_render;
        max_us = &max_us_schedule_to_render;
        sum_us = &sum_us_schedule_to_render;
        samples = &samples_schedule_to_render;
    } else {
        last_log_us = &last_log_us_render_to_swap;
        min_us = &min_us_render_to_swap;
        max_us = &max_us_render_to_swap;
        sum_us = &sum_us_render_to_swap;
        samples = &samples_render_to_swap;
    }

    const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (*min_us == 0 || delta_us < *min_us)
        *min_us = delta_us;
    if (delta_us > *max_us)
        *max_us = delta_us;
    *sum_us += delta_us;
    (*samples)++;

    if (*last_log_us == 0)
        *last_log_us = now_us;
    else if (now_us - *last_log_us >= 1000000 && *samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] " << label
            << " min_us=" << *min_us
            << " max_us=" << *max_us
            << " avg_us=" << (*sum_us / *samples)
            << " samples=" << *samples;
        *last_log_us = now_us;
        *min_us = 0;
        *max_us = 0;
        *sum_us = 0;
        *samples = 0;
    }
}

static void logScheduleToUpdateStats(qint64 now_us, qint64 delta_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] schedule_to_update min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logUpdateToRenderStats(qint64 now_us, qint64 delta_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] update_to_render min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logRenderPendingStats(qint64 now_us, const char *reason, bool render_active_now, int pending_during_cycle, qint64 swap_age_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static int pending_count = 0;

    pending_count++;
    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000) {
        qCInfo(chiakiGui).nospace()
            << "[latency] render_pending count=" << pending_count
            << " reason=" << reason
            << " render_active=" << render_active_now
            << " pending_during_cycle=" << pending_during_cycle
            << " swap_age_us=" << swap_age_us;
        last_log_us = now_us;
        pending_count = 0;
    }
}

static void logPendingFrameLifecycle(const char *stage, double pts, float duration, qint64 age_us, qint64 idle_gap_us, int queue_depth, int depth_limit, bool render_active_now, bool has_capacity)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static int total = 0;
    static int stored = 0;
    static int promoted = 0;
    static int deferred = 0;
    static int consumed = 0;
    static int blocked = 0;

    total++;
    if (strcmp(stage, "stored") == 0)
        stored++;
    else if (strcmp(stage, "promoted") == 0)
        promoted++;
    else if (strcmp(stage, "deferred") == 0)
        deferred++;
    else if (strcmp(stage, "consumed") == 0)
        consumed++;
    else if (strcmp(stage, "blocked") == 0)
        blocked++;

    const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (last_log_us == 0) {
        last_log_us = now_us;
        return;
    }
    if (now_us - last_log_us < 1000000)
        return;

    qCInfo(chiakiGui).nospace()
        << "[latency] pending_lifecycle stage=" << stage
        << " total=" << total
            << " stored=" << stored
            << " promoted=" << promoted
            << " deferred=" << deferred
            << " consumed=" << consumed
            << " blocked=" << blocked
            << " pts=" << pts
            << " duration=" << duration
            << " age_us=" << age_us
            << " idle_gap_us=" << idle_gap_us
            << " queue_depth=" << queue_depth
            << " depth_limit=" << depth_limit
        << " render_active=" << render_active_now
        << " has_capacity=" << has_capacity;

    total = 0;
    stored = 0;
    promoted = 0;
    deferred = 0;
    consumed = 0;
    blocked = 0;
    last_log_us = now_us;
}

static void logPendingFrameGate(const char *stage,
                                const char *reason,
                                double pts,
                                qint64 age_us,
                                int queue_depth,
                                int depth_limit,
                                bool render_active_now,
                                bool render_scheduled_now,
                                bool render_pending_now,
                                bool pending_exists,
                                bool has_capacity)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static int total = 0;
    static int render_busy = 0;
    static int queue_full = 0;
    static int other = 0;

    total++;
    if (strcmp(reason, "render_busy") == 0)
        render_busy++;
    else if (strcmp(reason, "queue_full") == 0)
        queue_full++;
    else
        other++;

    const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (last_log_us == 0) {
        last_log_us = now_us;
        return;
    }
    if (now_us - last_log_us < 1000000)
        return;

    qCInfo(chiakiGui).nospace()
        << "[latency] pending_gate stage=" << stage
        << " reason=" << reason
        << " total=" << total
        << " render_busy=" << render_busy
        << " queue_full=" << queue_full
        << " other=" << other
        << " pts=" << pts
        << " age_us=" << age_us
        << " queue_depth=" << queue_depth
        << " depth_limit=" << depth_limit
        << " render_active=" << render_active_now
        << " render_scheduled=" << render_scheduled_now
        << " render_pending=" << render_pending_now
        << " pending_exists=" << pending_exists
        << " has_capacity=" << has_capacity;

    total = 0;
    render_busy = 0;
    queue_full = 0;
    other = 0;
    last_log_us = now_us;
}

static void logPendingQueueDepthHold(const char *stage,
                                     qint64 now_us,
                                     int queue_depth,
                                     int depth_limit,
                                     bool pending_exists,
                                     bool render_active_now,
                                     bool render_scheduled_now,
                                     bool render_pending_now)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 hold_start_us = 0;
    static qint64 last_log_us = 0;
    static int hold_samples = 0;
    static int max_queue_depth = 0;

    const bool holding = pending_exists && queue_depth >= depth_limit;
    if (holding) {
        if (hold_start_us == 0) {
            hold_start_us = now_us;
            hold_samples = 0;
            max_queue_depth = queue_depth;
        }
        hold_samples++;
        if (queue_depth > max_queue_depth)
            max_queue_depth = queue_depth;
        if (last_log_us != 0 && now_us - last_log_us < 1000000)
            return;
        qCInfo(chiakiGui).nospace()
            << "[latency] pending_queue_hold stage=" << stage
            << " duration_us=" << (now_us - hold_start_us)
            << " samples=" << hold_samples
            << " queue_depth=" << queue_depth
            << " depth_limit=" << depth_limit
            << " max_queue_depth=" << max_queue_depth
            << " render_active=" << render_active_now
            << " render_scheduled=" << render_scheduled_now
            << " render_pending=" << render_pending_now;
        last_log_us = now_us;
        return;
    }

    if (hold_start_us == 0)
        return;

    qCInfo(chiakiGui).nospace()
        << "[latency] pending_queue_release stage=" << stage
        << " duration_us=" << (now_us - hold_start_us)
        << " samples=" << hold_samples
        << " queue_depth=" << queue_depth
        << " depth_limit=" << depth_limit
        << " max_queue_depth=" << max_queue_depth
        << " render_active=" << render_active_now
        << " render_scheduled=" << render_scheduled_now
        << " render_pending=" << render_pending_now;

    hold_start_us = 0;
    last_log_us = now_us;
    hold_samples = 0;
    max_queue_depth = 0;
}

static void logRenderPhaseStall(qint64 now_us,
                                const char *phase,
                                qint64 delta_us,
                                int queue_depth,
                                int depth_limit,
                                bool pending_frame_waiting,
                                bool render_active_now,
                                bool render_scheduled_now,
                                bool render_pending_now)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;
    static int max_queue_depth = 0;
    static int max_depth_limit = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;
    if (queue_depth > max_queue_depth)
        max_queue_depth = queue_depth;
    if (depth_limit > max_depth_limit)
        max_depth_limit = depth_limit;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] render_phase_stall phase=" << phase
            << " min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples
            << " max_queue_depth=" << max_queue_depth
            << " depth_limit=" << max_depth_limit
            << " pending_frame_waiting=" << pending_frame_waiting
            << " render_active=" << render_active_now
            << " render_scheduled=" << render_scheduled_now
            << " render_pending=" << render_pending_now;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
        max_queue_depth = 0;
        max_depth_limit = 0;
    }
}

static void logRenderHandoffStats(qint64 now_us,
                                  qint64 idle_age_us,
                                  qint64 request_age_us,
                                  int queue_depth,
                                  bool pending_frame_waiting,
                                  bool render_active_now,
                                  bool render_scheduled_now,
                                  bool render_pending_now)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_idle_age_us = 0;
    static qint64 max_idle_age_us = 0;
    static qint64 sum_idle_age_us = 0;
    static qint64 min_request_age_us = 0;
    static qint64 max_request_age_us = 0;
    static qint64 sum_request_age_us = 0;
    static int samples = 0;
    static int max_queue_depth = 0;

    if (min_idle_age_us == 0 || idle_age_us < min_idle_age_us)
        min_idle_age_us = idle_age_us;
    if (idle_age_us > max_idle_age_us)
        max_idle_age_us = idle_age_us;
    sum_idle_age_us += idle_age_us;

    if (min_request_age_us == 0 || request_age_us < min_request_age_us)
        min_request_age_us = request_age_us;
    if (request_age_us > max_request_age_us)
        max_request_age_us = request_age_us;
    sum_request_age_us += request_age_us;

    if (queue_depth > max_queue_depth)
        max_queue_depth = queue_depth;
    samples++;

    if (last_log_us == 0) {
        last_log_us = now_us;
        return;
    }
    if (now_us - last_log_us < 1000000 || samples == 0)
        return;

    qCInfo(chiakiGui).nospace()
        << "[latency] render_handoff idle_min_us=" << min_idle_age_us
        << " idle_max_us=" << max_idle_age_us
        << " idle_avg_us=" << (sum_idle_age_us / samples)
        << " request_min_us=" << min_request_age_us
        << " request_max_us=" << max_request_age_us
        << " request_avg_us=" << (sum_request_age_us / samples)
        << " samples=" << samples
        << " max_queue_depth=" << max_queue_depth
        << " pending_frame_waiting=" << pending_frame_waiting
        << " render_active=" << render_active_now
        << " render_scheduled=" << render_scheduled_now
        << " render_pending=" << render_pending_now;

    last_log_us = now_us;
    min_idle_age_us = 0;
    max_idle_age_us = 0;
    sum_idle_age_us = 0;
    min_request_age_us = 0;
    max_request_age_us = 0;
    sum_request_age_us = 0;
    samples = 0;
    max_queue_depth = 0;
}

static void logRenderQueueUpdateStats(qint64 now_us, qint64 delta_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] render_queue_update min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickRenderStats(qint64 now_us, qint64 delta_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] quick_render_work min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickBeginFrameStats(qint64 now_us, qint64 delta_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] quick_begin_frame min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickBeginHoldStats(qint64 now_us, qint64 delta_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] quick_begin_hold min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickBeginWaitStats(qint64 now_us, qint64 delta_us)
{
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] quick_begin_wait min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickBeginQtStats(qint64 now_us, qint64 delta_us)
{
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] quick_begin_qt min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickEndFrameStats(qint64 now_us, qint64 delta_us)
{
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] quick_end_frame min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickEndQtStats(qint64 now_us, qint64 delta_us)
{
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] quick_end_qt min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickReleaseStats(qint64 now_us, qint64 delta_us)
{
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] quick_release min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logSwapToStartGapStats(qint64 now_us, qint64 delta_us)
{
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] swap_to_start_gap min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logRenderSetupStats(qint64 now_us, qint64 delta_us)
{
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        qCInfo(chiakiGui).nospace()
            << "[latency] render_setup min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logDroppedFrameReason(const char *reason, qint64 now_us, double pts, const char *detail = nullptr)
{
    Q_UNUSED(now_us);
    qCInfo(chiakiGui).nospace()
        << "[drop] reason=" << reason
        << " pts=" << pts
        << (detail ? " detail=" : "")
        << (detail ? detail : "");
}

static const char *updateRequestReasonName(int reason)
{
    switch (static_cast<QmlMainWindow::UpdateRequestReason>(reason)) {
    case QmlMainWindow::UpdateRequestReason::SceneChanged:
        return "scene_changed";
    case QmlMainWindow::UpdateRequestReason::RenderRequested:
        return "render_requested";
    case QmlMainWindow::UpdateRequestReason::Timer:
        return "timer";
    case QmlMainWindow::UpdateRequestReason::QueueStoredFrame:
        return "queue_stored_frame";
    case QmlMainWindow::UpdateRequestReason::QueueReset:
        return "queue_reset";
    case QmlMainWindow::UpdateRequestReason::PendingFrame:
        return "pending_frame";
    case QmlMainWindow::UpdateRequestReason::Replay:
        return "replay";
    case QmlMainWindow::UpdateRequestReason::PlaceboReset:
        return "placebo_reset";
    case QmlMainWindow::UpdateRequestReason::FinalizePending:
        return "finalize_pending";
    case QmlMainWindow::UpdateRequestReason::NoSwapchain:
        return "no_swapchain";
    case QmlMainWindow::UpdateRequestReason::VSync:
        return "vsync";
    case QmlMainWindow::UpdateRequestReason::Unknown:
    default:
        return "unknown";
    }
}

[[maybe_unused]] static void logUpdateRequestReasonStats(int reason)
{
    static qint64 last_log_us = 0;
    static std::array<int, 11> blocked_counts{};
    static int total_blocked = 0;

    const int idx = static_cast<int>(reason);
    if (idx >= 0 && idx < static_cast<int>(blocked_counts.size())) {
        blocked_counts[idx]++;
        total_blocked++;
    }

    const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (last_log_us == 0) {
        last_log_us = now_us;
        return;
    }
    if (now_us - last_log_us < 1000000)
        return;

    qCInfo(chiakiGui).nospace()
        << "[latency] update_blocked total=" << total_blocked
        << " scene_changed=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::SceneChanged)]
        << " render_requested=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::RenderRequested)]
        << " timer=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::Timer)]
        << " queue_stored_frame=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::QueueStoredFrame)]
        << " queue_reset=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::QueueReset)]
        << " pending_frame=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::PendingFrame)]
        << " replay=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::Replay)]
        << " placebo_reset=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::PlaceboReset)]
        << " finalize_pending=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::FinalizePending)]
        << " no_swapchain=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::NoSwapchain)]
        << " vsync=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::VSync)]
        << " unknown=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::Unknown)];

    blocked_counts.fill(0);
    total_blocked = 0;
    last_log_us = now_us;
}

static void logRenderRequestedSignalStats(bool render_scheduled_now, bool render_active_now, bool quick_need_render_now)
{
    static qint64 last_log_us = 0;
    static int total = 0;
    static int while_scheduled = 0;
    static int while_active = 0;
    static int while_both = 0;
    static int while_idle = 0;

    total++;
    if (render_scheduled_now && render_active_now)
        while_both++;
    else if (render_scheduled_now)
        while_scheduled++;
    else if (render_active_now)
        while_active++;
    else
        while_idle++;

    Q_UNUSED(quick_need_render_now);

    const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (last_log_us == 0) {
        last_log_us = now_us;
        return;
    }
    if (now_us - last_log_us < 1000000)
        return;

    qCInfo(chiakiGui).nospace()
        << "[latency] render_requested_signal total=" << total
        << " while_scheduled=" << while_scheduled
        << " while_active=" << while_active
        << " while_both=" << while_both
        << " while_idle=" << while_idle;

    total = 0;
    while_scheduled = 0;
    while_active = 0;
    while_both = 0;
    while_idle = 0;
    last_log_us = now_us;
}

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

static void logRenderIdleStats(qint64 now_us, qint64 render_duration_us, bool render_pending_now, bool pending_frame_waiting_now, int pending_during_cycle)
{
    static qint64 last_log_us = 0;
    static qint64 min_duration_us = 0;
    static qint64 max_duration_us = 0;
    static qint64 sum_duration_us = 0;
    static int samples = 0;
    static int pending_samples = 0;
    static int pending_frame_samples = 0;
    static int pending_cycle_samples = 0;

    if (min_duration_us == 0 || render_duration_us < min_duration_us)
        min_duration_us = render_duration_us;
    if (render_duration_us > max_duration_us)
        max_duration_us = render_duration_us;
    sum_duration_us += render_duration_us;
    samples++;
    if (render_pending_now)
        pending_samples++;
    if (pending_frame_waiting_now)
        pending_frame_samples++;
    if (pending_during_cycle > 0)
        pending_cycle_samples++;

    if (last_log_us == 0) {
        last_log_us = now_us;
        return;
    }
    if (now_us - last_log_us < 1000000 || samples == 0)
        return;

    qCInfo(chiakiGui).nospace()
        << "[latency] render_idle duration_us_min=" << min_duration_us
        << " duration_us_max=" << max_duration_us
        << " duration_us_avg=" << (sum_duration_us / samples)
        << " samples=" << samples
        << " pending_samples=" << pending_samples
        << " pending_frame_samples=" << pending_frame_samples
        << " pending_cycle_samples=" << pending_cycle_samples;

    last_log_us = now_us;
    min_duration_us = 0;
    max_duration_us = 0;
    sum_duration_us = 0;
    samples = 0;
    pending_samples = 0;
    pending_frame_samples = 0;
    pending_cycle_samples = 0;
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
    if (out_frame->num_planes <= 0) {
        qCWarning(chiakiGui) << "Mapped frame contains no planes, dropping";
        pl_unmap_avframe(gpu, out_frame);
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
    const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    QmlMainWindow *q = frame ? reinterpret_cast<QmlMainWindow *>(frame->opaque) : nullptr;
    const QString detail = q ? q->describePlaceboDiscardReason(now_us)
                             : QStringLiteral("cause=owner_missing");
    const QByteArray detail_utf8 = detail.toUtf8();
    logDroppedFrameReason("placebo_discard", now_us, src->pts, detail_utf8.constData());
    if (q)
        q->increaseDroppedFrames();
    av_frame_free(&frame);
    qCDebug(chiakiGui) << "Dropped frame with PTS" << src->pts;
}

static void discard_snapshot_frame(const struct pl_source_frame *src)
{
    AVFrame *frame = reinterpret_cast<AVFrame *>(src->frame_data);
    av_frame_free(&frame);
}

static AVFrame *clone_frame_for_replay(const AVFrame *frame)
{
    if (!frame)
        return nullptr;

    return av_frame_clone(frame);
}

static AVFrame *make_fallback_snapshot_frame(const AVFrame *frame)
{
    if (!frame)
        return nullptr;

    if (!frame->hw_frames_ctx || frame->format == AV_PIX_FMT_VULKAN)
        return nullptr;

    AVFrame *copy = av_frame_alloc();
    if (!copy)
        return nullptr;

    if (av_hwframe_transfer_data(copy, frame, 0) < 0) {
        av_frame_free(&copy);
        return av_frame_clone(frame);
    }

    int sw_format = copy->format;
    int sw_width = copy->width;
    int sw_height = copy->height;
    AVRational sw_sar = copy->sample_aspect_ratio;
    AVColorSpace sw_colorspace = copy->colorspace;
    AVColorRange sw_color_range = copy->color_range;
    AVColorPrimaries sw_color_primaries = copy->color_primaries;
    AVColorTransferCharacteristic sw_color_trc = copy->color_trc;
    av_frame_copy_props(copy, frame);
    copy->format = sw_format >= 0 ? sw_format : frame->format;
    copy->width = sw_width;
    copy->height = sw_height;
    copy->sample_aspect_ratio = sw_sar;
    copy->colorspace = sw_colorspace;
    copy->color_range = sw_color_range;
    copy->color_primaries = sw_color_primaries;
    copy->color_trc = sw_color_trc;
    if (copy->hw_frames_ctx) {
        av_buffer_unref(&copy->hw_frames_ctx);
        copy->hw_frames_ctx = nullptr;
    }
    return copy;
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

static constexpr float spatial_upscale_threshold = 1.2f;

static bool uses_custom_upscale_hook(PlaceboUpscaler upscaler)
{
    return upscaler == PlaceboUpscaler::FSR || uses_fsrcnnx(upscaler);
}

static const struct pl_hook *select_spatial_hook(QmlMainWindow::VideoPreset preset,
                                                 PlaceboUpscaler upscaler,
                                                 float upscale_factor,
                                                 const struct pl_hook *fsr_hook,
                                                 const struct pl_hook *hook8,
                                                 const struct pl_hook *hook16)
{
    if (upscale_factor <= 1.0f)
        return nullptr;

    switch (preset) {
    case QmlMainWindow::VideoPreset::HighQualitySpatial:
        return upscale_factor >= spatial_upscale_threshold ? hook8 : fsr_hook;
    case QmlMainWindow::VideoPreset::HighQualityAdvancedSpatial:
        return upscale_factor >= spatial_upscale_threshold ? hook16 : fsr_hook;
    case QmlMainWindow::VideoPreset::Custom:
        switch (upscaler) {
        case PlaceboUpscaler::FSR:
            return fsr_hook;
        case PlaceboUpscaler::FSRCNNX8:
            return upscale_factor >= spatial_upscale_threshold ? hook8 : fsr_hook;
        case PlaceboUpscaler::FSRCNNX16:
            return upscale_factor >= spatial_upscale_threshold ? hook16 : fsr_hook;
        default:
            return nullptr;
        }
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

    drainRenderThread();

    if (buffered_pace_thread) {
        buffered_pace_thread->stopThread();
        buffered_pace_thread->wait();
        delete buffered_pace_thread;
        buffered_pace_thread = nullptr;
    }
    if (present_pace_thread) {
        present_pace_thread->stopThread();
        present_pace_thread->wait();
        delete present_pace_thread;
        present_pace_thread = nullptr;
    }
    if (deferred_swap_thread) {
        deferred_swap_thread->stopThread();
        deferred_swap_thread->wait();
        delete deferred_swap_thread;
        deferred_swap_thread = nullptr;
    }

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
        if (quick_tex)
            pl_tex_destroy(gpu, &quick_tex);
        for (auto &tex : placebo_tex)
            pl_tex_destroy(gpu, &tex);
        if (render_backend == RenderBackend::Vulkan && quick_sem)
            pl_vulkan_sem_destroy(gpu, &quick_sem);
    }

    {
        QMutexLocker locker(&pending_frame_mutex);
        if (pending_frame)
            av_frame_free(&pending_frame);
        pending_frame_stored_us = 0;
        pending_pts = 0.0;
        pending_duration = 0.0f;
        pending_frame_queue_origin = 0.0;
        pending_frame_present.storeRelease(0);
    }
    {
        QMutexLocker locker(&reset_seed_mutex);
        if (reset_seed_frame)
            av_frame_free(&reset_seed_frame);
        reset_seed_pts = 0.0;
        reset_seed_duration = 0.0f;
        reset_seed_generation = 0;
    }
    clearSnapshotFrame();

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
    quick_tex = {};
    quick_sem = VK_NULL_HANDLE;
    quick_sem_value = 0;
    quick_vk_image = VK_NULL_HANDLE;
    quick_vk_format = VK_FORMAT_UNDEFINED;

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
    schedule_frame_mixer_active.storeRelaxed(configuredFrameMixerEnabledForScheduling() ? 1 : 0);
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

namespace {
constexpr int kDefaultQueueDepthLimit = 2;
constexpr int kAdaptiveQueueDepthBoost = 1;

static const struct pl_filter_config *frame_mixer_config(Settings *settings)
{
    if (!settings)
        return pl_find_filter_config("oversample", PL_FILTER_FRAME_MIXING);

    switch (settings->GetPlaceboFrameMixer()) {
    case PlaceboFrameMixer::None:
        return nullptr;
    case PlaceboFrameMixer::Oversample:
        return pl_find_filter_config("oversample", PL_FILTER_FRAME_MIXING);
    case PlaceboFrameMixer::Hermite:
        return pl_find_filter_config("hermite", PL_FILTER_FRAME_MIXING);
    case PlaceboFrameMixer::Linear:
        return pl_find_filter_config("linear", PL_FILTER_FRAME_MIXING);
    case PlaceboFrameMixer::Cubic:
        return pl_find_filter_config("cubic", PL_FILTER_FRAME_MIXING);
    }

    return pl_find_filter_config("oversample", PL_FILTER_FRAME_MIXING);
}

}

const struct pl_filter_config *QmlMainWindow::effectiveFrameMixerConfig(const struct pl_render_params *render_params) const
{
    Q_UNUSED(render_params);

    switch (video_preset) {
    case VideoPreset::Fast:
        return frame_mixer_config(settings);
    case VideoPreset::Custom: {
        return frame_mixer_config(settings);
    }
    case VideoPreset::Default:
    case VideoPreset::HighQuality:
    case VideoPreset::HighQualitySpatial:
    case VideoPreset::HighQualityAdvancedSpatial:
        return frame_mixer_config(settings);
    }

    return frame_mixer_config(settings);
}

bool QmlMainWindow::effectiveFrameMixerEnabled(const struct pl_render_params *render_params) const
{
    return effectiveFrameMixerConfig(render_params) != nullptr;
}

bool QmlMainWindow::configuredFrameMixerEnabledForScheduling() const
{
    switch (video_preset) {
    case VideoPreset::Fast:
        return frame_mixer_config(settings) != nullptr;
    case VideoPreset::Custom: {
        return frame_mixer_config(settings) != nullptr;
    }
    case VideoPreset::Default:
    case VideoPreset::HighQuality:
    case VideoPreset::HighQualitySpatial:
    case VideoPreset::HighQualityAdvancedSpatial:
        return frame_mixer_config(settings) != nullptr;
    }

    return frame_mixer_config(settings) != nullptr;
}

void QmlMainWindow::presentFrame(ChiakiFfmpegFrame frame, int32_t frames_lost)
{
    dropped_frames_current.fetchAndAddRelaxed(frames_lost);

    if (!frame.frame)
        return;

    if (frame.recovered)
    {
        const bool stored_pending = storePendingFrame(frame, true);
        if (stored_pending)
            snapshotPendingFrame();
        if (frame.frame)
            av_frame_free(&frame.frame);
        bool need_reset = false;
        double queue_pts_origin_snapshot = -1.0;
        {
            QMutexLocker locker(&placebo_state_mutex);
            queue_pts_origin_snapshot = queue_pts_origin;
            double current_pts = queue_pts_origin;
            if (queue_pts_origin >= 0.0 && playback_started && ts_start) {
                const uint64_t now_us = chiaki_time_now_monotonic_us();
                current_pts += static_cast<double>(now_us - ts_start) / 1000000.0;
            }
            need_reset = queue_pts_origin < 0.0 || frame.pts + 1e-6 < current_pts;
        }
        if (need_reset && placebo_reset_pending.loadAcquire() == 0) {
            logPtsRollback("presentFrame/recovered", frame.pts, queue_pts_origin_snapshot, true, frame.duration * 1000.0);
            queuePlaceboReset(true);
        }
        if (need_reset)
            storeResetSeedFromPendingFrame(reset_seed_capture_generation.loadAcquire());
        return;
    }

    frame.frame->opaque = this;

    if (frame.duration > 0.0f)
        source_frame_interval_ms = 1000.0 * frame.duration;
    bool deinterlace_enabled = false;
    {
        QMutexLocker locker(&placebo_state_mutex);
        if (queue_pts_origin < 0.0 || frame.pts + 1e-6 < queue_pts_origin) {
            logPtsRollback("presentFrame/rollback", frame.pts, queue_pts_origin, false, frame.duration * 1000.0);
            pl_queue_reset(placebo_queue);
            ts_start = 0;
            queue_pts_origin = frame.pts;
            newest_queued_frame_pts = -1.0;
            playback_started = false;
        }
        deinterlace_enabled = this->renderparams_opts->params.deinterlace_params;
    }

    struct pl_source_frame src_frame{
        .pts = frame.pts - queue_pts_origin,
        .duration = static_cast<float>(frame.duration),
        // allow soft-disabling deinterlacing at the source frame level
        .first_field = deinterlace_enabled
            ? pl_field_from_avframe(frame.frame)
                : PL_FIELD_NONE,
        .frame_data = frame.frame,
        .map = map_frame,
        .unmap = unmap_frame,
        .discard = discard_frame,
    };
    const int depth_limit = effectiveQueueDepthLimit();
    bool queued = false;
    bool depth_exceeded = false;
    bool promote_pending = false;
    const bool render_busy = render_active.loadAcquire() != 0;
    bool render_scheduled_now = false;
    bool render_pending_now = false;
    const bool should_rearm_stored_frame = [&]() {
        QMutexLocker locker(&render_schedule_mutex);
        render_scheduled_now = render_scheduled;
        render_pending_now = render_pending;
        return !render_busy &&
               !render_scheduled &&
               !render_pending &&
               queue_stored_frame_pending.loadAcquire() == 0 &&
               pending_frame_present.loadAcquire() == 0;
    }();
    AVFrame *promoted_frame = nullptr;
    double promoted_pts = 0.0;
    float promoted_duration = 0.0f;
    double promoted_queue_origin = 0.0;
    bool promoted_deinterlace_enabled = false;
    qint64 promoted_age_us = -1;
    int queue_depth_snapshot = -1;
    bool pending_exists_snapshot = false;
    bool promote_eligible_snapshot = false;
    bool has_capacity_snapshot = false;
    qint64 idle_gap_us_snapshot = -1;
    {
        QMutexLocker locker(&placebo_state_mutex);
        const int depth = pl_queue_num_frames(placebo_queue);
        queue_depth_snapshot = depth;
        const bool pending_exists = pending_frame_present.loadAcquire() != 0;
        pending_exists_snapshot = pending_exists;
        const int tolerated_depth = depth_limit + (pending_exists ? 1 : 0);
        depth_exceeded = depth >= tolerated_depth;
        has_capacity_snapshot = depth < depth_limit;
        promote_eligible_snapshot = !render_busy && pending_exists && has_capacity_snapshot;
        updateQueueDepthAverage(depth);
        if (promote_eligible_snapshot) {
            QMutexLocker pending_locker(&pending_frame_mutex);
            if (pending_frame) {
                promote_pending = true;
                promoted_frame = pending_frame;
                promoted_pts = pending_pts;
                promoted_duration = pending_duration;
                promoted_queue_origin = pending_frame_queue_origin;
                promoted_deinterlace_enabled = deinterlace_enabled;
                promoted_age_us = pending_frame_stored_us > 0
                    ? static_cast<qint64>(chiaki_time_now_monotonic_us()) - static_cast<qint64>(pending_frame_stored_us)
                    : -1;
                pending_frame = nullptr;
                pending_pts = 0.0;
                pending_duration = 0.0f;
                pending_frame_queue_origin = 0.0;
                pending_frame_stored_us = 0;
                pending_frame_present.storeRelease(0);
            }
        }
        if (!depth_exceeded && !promote_pending && !pending_exists)
        {
            const qint64 push_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            pl_queue_push(placebo_queue, &src_frame);
            const qint64 push_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            logLatencyStats("pl_queue_push", push_end_us - push_begin_us);
            qCInfo(chiakiGui).nospace()
                << "[latency] pl_queue_push_us=" << (push_end_us - push_begin_us)
                << " queue_depth=" << pl_queue_num_frames(placebo_queue);
            newest_queued_frame_pts = qMax(newest_queued_frame_pts, frame.pts);
            queued = true;
        }
    }
    {
        const qint64 idle_us = last_render_idle_us.loadAcquire();
        const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        idle_gap_us_snapshot = idle_us > 0 ? qMax<qint64>(0, now_us - idle_us) : -1;
    }

    if (pending_exists_snapshot && !promote_eligible_snapshot) {
        const char *gate_reason = render_busy ? "render_busy"
                                              : (!has_capacity_snapshot ? "queue_full" : "promotion_window");
        qint64 blocked_age_us = -1;
        double blocked_pts = 0.0;
        float blocked_duration = 0.0f;
        {
            QMutexLocker pending_locker(&pending_frame_mutex);
            if (pending_frame) {
                blocked_pts = pending_pts;
                blocked_duration = pending_duration;
                blocked_age_us = pending_frame_stored_us > 0
                    ? static_cast<qint64>(chiaki_time_now_monotonic_us()) - static_cast<qint64>(pending_frame_stored_us)
                    : -1;
            }
        }
        logPendingFrameLifecycle("blocked",
                                 blocked_pts,
                                 blocked_duration,
                                 blocked_age_us,
                                 idle_gap_us_snapshot,
                                 queue_depth_snapshot,
                                 depth_limit,
                                 render_busy,
                                 has_capacity_snapshot);
        logPendingFrameGate("blocked",
                            gate_reason,
                            blocked_pts,
                            blocked_age_us,
                            queue_depth_snapshot,
                            depth_limit,
                            render_busy,
                            render_scheduled_now,
                            render_pending_now,
                            pending_exists_snapshot,
                            has_capacity_snapshot);
    }
    if (promote_pending) {
        bool queued_after_promotion = false;
        if (promoted_frame) {
            struct pl_source_frame promoted_src_frame = {
                .pts = promoted_pts - promoted_queue_origin,
                .duration = promoted_duration,
                .first_field = promoted_deinterlace_enabled
                    ? pl_field_from_avframe(promoted_frame)
                    : PL_FIELD_NONE,
                .frame_data = promoted_frame,
                .map = map_frame,
                .unmap = unmap_frame,
                .discard = discard_frame,
            };
            {
                QMutexLocker locker(&placebo_state_mutex);
                {
                    const qint64 push_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                    pl_queue_push(placebo_queue, &promoted_src_frame);
                    const qint64 push_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                    logLatencyStats("pl_queue_push", push_end_us - push_begin_us);
                    qCInfo(chiakiGui).nospace()
                        << "[latency] pl_queue_push_promoted_us=" << (push_end_us - push_begin_us)
                        << " queue_depth=" << pl_queue_num_frames(placebo_queue);
                    newest_queued_frame_pts = qMax(newest_queued_frame_pts, promoted_pts);
                    if (pl_queue_num_frames(placebo_queue) < depth_limit) {
                        const qint64 push2_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                        pl_queue_push(placebo_queue, &src_frame);
                        const qint64 push2_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                        logLatencyStats("pl_queue_push", push2_end_us - push2_begin_us);
                        qCInfo(chiakiGui).nospace()
                            << "[latency] pl_queue_push_after_promote_us=" << (push2_end_us - push2_begin_us)
                            << " queue_depth=" << pl_queue_num_frames(placebo_queue);
                        newest_queued_frame_pts = qMax(newest_queued_frame_pts, frame.pts);
                        queued = true;
                        queued_after_promotion = true;
                    }
                }
            }
            logPendingFrameLifecycle("promoted",
                                     promoted_pts,
                                     promoted_duration,
                                     promoted_age_us,
                                     idle_gap_us_snapshot,
                                     queue_depth_snapshot,
                                     depth_limit,
                                     render_busy,
                                     true);
        }
        if (!queued_after_promotion) {
            if (storePendingFrame(frame, true))
                snapshotPendingFrame();
            if (frame.frame)
                av_frame_free(&frame.frame);
            if (should_rearm_stored_frame)
                scheduleUpdate(false, UpdateRequestReason::QueueStoredFrame);
            return;
        }
    }
    if (!queued)
    {
        const char *gate_reason = pending_exists_snapshot ? "pending_exists" : (depth_exceeded ? "queue_full" : "not_queued");
        logPendingFrameGate("store_pending",
                            gate_reason,
                            frame.pts,
                            -1,
                            queue_depth_snapshot,
                            depth_limit,
                            render_busy,
                            render_scheduled_now,
                            render_pending_now,
                            pending_exists_snapshot,
                            has_capacity_snapshot);
        const quint64 reset_seed_generation = reset_seed_capture_generation.loadAcquire();
        if (reset_seed_capture_active.loadAcquire() != 0 && reset_seed_generation != 0)
            storeResetSeedFrame(frame.frame, frame.pts, frame.duration, reset_seed_generation);
        if (storePendingFrame(frame, true))
            snapshotPendingFrame();
        if (frame.frame)
            av_frame_free(&frame.frame);
        if (should_rearm_stored_frame)
            scheduleUpdate(false, UpdateRequestReason::QueueStoredFrame);
        return;
    }

    bool dropped_stale_pending = false;
    {
        QMutexLocker locker(&pending_frame_mutex);
        if (pending_frame && pending_pts <= frame.pts + 1e-6) {
            logDroppedFrameReason("stale_pending_frame", static_cast<qint64>(chiaki_time_now_monotonic_us()), pending_pts,
                                  "replaced by newer direct queue submission");
            qCDebug(chiakiGui)
                << "Dropping stale pending frame after newer direct queue submission"
                << "pending_pts=" << pending_pts
                << "queued_pts=" << frame.pts;
            av_frame_free(&pending_frame);
            pending_pts = 0.0;
            pending_duration = 0.0f;
            pending_frame_queue_origin = 0.0;
            pending_frame_stored_us = 0;
            pending_frame_present.storeRelease(0);
            dropped_stale_pending = true;
        }
    }
    if (dropped_stale_pending)
        updatePendingFrameAge(0.0);

    {
        const quint64 reset_seed_generation = reset_seed_capture_generation.loadAcquire();
        if (reset_seed_capture_active.loadAcquire() != 0 && reset_seed_generation != 0)
            storeResetSeedFrame(frame.frame, frame.pts, frame.duration, reset_seed_generation);
    }

    snapshotLastFrame(frame.frame, frame.pts, frame.duration);

    if (!playback_started) {
        uint64_t now_us = chiaki_time_now_monotonic_us();
        uint64_t frame_pts_us = src_frame.pts > 0.0
            ? static_cast<uint64_t>(src_frame.pts * 1000000.0)
            : 0;
        ts_start = frame_pts_us < now_us ? now_us - frame_pts_us : 0;
        playback_started = true;
    }

    if (!has_video) {
        has_video = true;
        if (!grab_input && settings->GetHideCursor())
            setCursor(Qt::BlankCursor);
        emit hasVideoChanged();
    }
    if (queued) {
        if (should_rearm_stored_frame)
            scheduleUpdate(false, UpdateRequestReason::QueueStoredFrame);
    }
}

void QmlMainWindow::setStreamMaxFPS(unsigned int max_fps)
{
    const double interval_ms = max_fps > 0 ? 1000.0 / max_fps : 16.6667;
    stream_configured_frame_interval_ms = interval_ms;
    source_frame_interval_ms = interval_ms;
}

void QmlMainWindow::resetPlaceboQueue()
{
    uint64_t now_ms = chiaki_time_now_monotonic_ms();
    const bool preserve_timeline = placebo_reset_preserve_timeline.loadAcquire() != 0;
    if (!preserve_timeline && last_placebo_reset_ts && now_ms - last_placebo_reset_ts < 100)
    {
        const int wait_ms = static_cast<int>(qMax<uint64_t>(100 - (now_ms - last_placebo_reset_ts), 1));
        const quint64 generation = placebo_reset_throttle_generation.fetchAndAddRelaxed(1) + 1;
        qCInfo(chiakiGui)
            << "Queue reset throttled"
            << "wait_ms=" << wait_ms
            << "preserve_timeline=" << preserve_timeline;
        placebo_reset_pending.storeRelease(0);
        QTimer::singleShot(wait_ms, this, [this, generation, preserve_timeline]() {
            if (placebo_reset_throttle_generation.loadAcquire() != generation)
                return;
            queuePlaceboReset(preserve_timeline);
        });
        return;
    }
    last_placebo_reset_ts = now_ms;
    placebo_reset_throttle_generation.fetchAndAddRelaxed(1);
    placebo_reset_preserve_timeline.storeRelease(0);

    QMutexLocker locker(&placebo_state_mutex);
    qCInfo(chiakiGui)
        << "Queue reset executing"
        << "preserve_timeline=" << preserve_timeline
        << "queue_pts_origin=" << queue_pts_origin
        << "playback_started=" << playback_started;
    pl_queue_reset(placebo_queue);
    newest_queued_frame_pts = -1.0;
    const bool keep_timeline = preserve_timeline && queue_pts_origin >= 0.0 && playback_started;
    preserve_playback_timeline = keep_timeline;
    if (!keep_timeline) {
        queue_pts_origin = -1.0;
        playback_started = false;
        ts_start = 0;
        preserve_playback_timeline = false;
    }
    locker.unlock();
    const quint64 generation = pending_reset_snapshot_generation.loadRelaxed();
    last_reset_snapshot_generation.storeRelaxed(generation);
    const bool had_pending = hasPendingFrame();
    const quint64 reset_generation = reset_seed_capture_generation.loadAcquire();
    reset_seed_capture_active.storeRelease(0);
    reset_seed_capture_generation.storeRelease(0);
    bool seeded = applyResetSeedFrame(reset_generation);
    if (!seeded && had_pending)
        applyPendingFrame();
    if (!seeded && !had_pending)
        applyKeptFrameSnapshot();
}

void QmlMainWindow::schedulePlaceboReset()
{
    queuePlaceboReset(false);
}

void QmlMainWindow::queuePlaceboReset(bool preserve_timeline)
{
    qCInfo(chiakiGui)
        << "Queue reset scheduled"
        << "preserve_timeline=" << preserve_timeline;
    if (preserve_timeline) {
        placebo_reset_preserve_timeline.storeRelease(1);
    } else if (placebo_reset_preserve_timeline.loadRelaxed() == 0) {
        placebo_reset_preserve_timeline.storeRelease(0);
    }
    quint64 generation = reset_seed_capture_generation.loadAcquire();
    if (!generation)
        generation = placebo_reset_throttle_generation.loadRelaxed() + 1;
    reset_seed_capture_generation.storeRelease(generation);
    reset_seed_capture_active.storeRelease(1);
    placebo_reset_pending.storeRelease(1);
    QMetaObject::invokeMethod(this, [this]() {
        scheduleUpdate(false, UpdateRequestReason::PlaceboReset);
    }, Qt::QueuedConnection);
}

double QmlMainWindow::queueDepthAverage() const
{
    return queue_depth_average;
}

double QmlMainWindow::pendingFrameAge() const
{
    QMutexLocker locker(&pending_frame_age_mutex);
    return pending_frame_age;
}

QString QmlMainWindow::describePlaceboDiscardReason(qint64 now_us) const
{
    const qint64 swap_return_us = last_swap_return_us.loadAcquire();
    const qint64 swap_age_us = swap_return_us > 0 ? qMax<qint64>(0, now_us - swap_return_us) : -1;
    const qint64 frame_age_us = pending_frame_stored_us > 0 ? qMax<qint64>(0, now_us - static_cast<qint64>(pending_frame_stored_us)) : -1;
    const bool pending_exists = hasPendingFrame();
    const int render_active_now = render_active.loadAcquire();
    const int pending_during_cycle = render_pending_during_cycle.loadAcquire();
    const qint64 queue_depth_avg_scaled = static_cast<qint64>(queue_depth_average * 1000.0);
    const qint64 pending_age_scaled = static_cast<qint64>(pendingFrameAge() * 1000.0);

    QString cause;
    if (!present_vsync_enabled) {
        cause = QStringLiteral("present_vsync_disabled");
    } else if (pending_exists) {
        cause = QStringLiteral("pending_frame_waiting");
    } else if (render_active_now != 0 || pending_during_cycle > 0) {
        cause = QStringLiteral("render_busy");
    } else if (swap_age_us >= 0 && swap_age_us > 20000) {
        cause = QStringLiteral("late_acquire_or_missed_present_window");
    } else {
        cause = QStringLiteral("present_window_miss");
    }

    return QStringLiteral("cause=%1 swap_age_us=%2 frame_age_us=%3 queue_depth_avg_x1000=%4 pending_age_x1000=%5 render_active=%6 pending_during_cycle=%7 vsync=%8")
        .arg(cause)
        .arg(swap_age_us)
        .arg(frame_age_us)
        .arg(queue_depth_avg_scaled)
        .arg(pending_age_scaled)
        .arg(render_active_now)
        .arg(pending_during_cycle)
        .arg(present_vsync_enabled ? 1 : 0);
}

void QmlMainWindow::updateQueueDepthAverage(int depth)
{
    const double alpha = 0.15;
    double next = queue_depth_average * (1.0 - alpha) + static_cast<double>(depth) * alpha;
    queue_depth_average = next;
    emit queueDepthAverageChanged();
}

void QmlMainWindow::updatePendingFrameAge(double age)
{
    double clamped = qMax(age, 0.0);
    {
        QMutexLocker locker(&pending_frame_age_mutex);
        pending_frame_age = clamped;
    }
    emit pendingFrameAgeChanged();
}

bool QmlMainWindow::storePendingFrame(ChiakiFfmpegFrame &frame, bool take_ownership)
{
    if (!frame.frame)
        return false;

    int queue_depth = -1;
    int depth_limit = effectiveQueueDepthLimit();
    {
        QMutexLocker locker(&pending_frame_mutex);
        // Keep the freshest queued-backlog frame and ignore stale arrivals.
        if (pending_frame && frame.pts <= pending_pts + 1e-6)
            return false;
    }

    AVFrame *stored_frame = take_ownership ? frame.frame : av_frame_clone(frame.frame);
    if (!stored_frame)
        return false;

    double queue_origin = 0.0;
    {
        QMutexLocker state_locker(&placebo_state_mutex);
        queue_origin = queue_pts_origin;
        queue_depth = pl_queue_num_frames(placebo_queue);
    }

    {
        QMutexLocker locker(&pending_frame_mutex);
        if (pending_frame && frame.pts <= pending_pts + 1e-6)
        {
            if (!take_ownership)
                av_frame_free(&stored_frame);
            return false;
        }
        if (pending_frame)
            av_frame_free(&pending_frame);
        pending_frame = stored_frame;
        pending_pts = frame.pts;
        pending_duration = frame.duration;
        pending_frame_queue_origin = queue_origin;
        pending_frame_stored_us = chiaki_time_now_monotonic_us();
        pending_frame_present.storeRelease(1);
    }

    if (take_ownership)
        frame.frame = nullptr;

    const qint64 stored_idle_us = last_render_idle_us.loadAcquire();
    const qint64 stored_now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    const qint64 stored_idle_gap_us = stored_idle_us > 0 ? qMax<qint64>(0, stored_now_us - stored_idle_us) : -1;
    logPendingFrameLifecycle("stored",
                             frame.pts,
                             frame.duration,
                             0,
                             stored_idle_gap_us,
                             queue_depth,
                             depth_limit,
                             render_active.loadAcquire() != 0,
                             queue_depth < depth_limit);
    refreshPendingFrameAge();
    return true;
}

bool QmlMainWindow::storeResetSeedFrame(const AVFrame *frame, double pts, float duration, quint64 generation)
{
    if (!frame || generation == 0)
        return false;

    AVFrame *clone = av_frame_clone(frame);
    if (!clone)
        return false;

    {
        QMutexLocker locker(&reset_seed_mutex);
        if (reset_seed_frame && reset_seed_generation != generation) {
            av_frame_free(&reset_seed_frame);
            reset_seed_frame = nullptr;
            reset_seed_pts = 0.0;
            reset_seed_duration = 0.0f;
            reset_seed_generation = 0;
        }
        if (reset_seed_frame && pts <= reset_seed_pts + 1e-6) {
            av_frame_free(&clone);
            return false;
        }
        if (reset_seed_frame)
            av_frame_free(&reset_seed_frame);
        reset_seed_frame = clone;
        reset_seed_pts = pts;
        reset_seed_duration = duration;
        reset_seed_generation = generation;
    }

    return true;
}

bool QmlMainWindow::storeResetSeedFromPendingFrame(quint64 generation)
{
    if (generation == 0)
        return false;

    AVFrame *clone = nullptr;
    double pts = 0.0;
    float duration = 0.0f;
    {
        QMutexLocker locker(&pending_frame_mutex);
        if (!pending_frame)
            return false;
        clone = av_frame_clone(pending_frame);
        if (!clone)
            return false;
        pts = pending_pts;
        duration = pending_duration;
    }

    {
        QMutexLocker locker(&reset_seed_mutex);
        if (reset_seed_frame && reset_seed_generation != generation) {
            av_frame_free(&reset_seed_frame);
            reset_seed_frame = nullptr;
            reset_seed_pts = 0.0;
            reset_seed_duration = 0.0f;
            reset_seed_generation = 0;
        }
        if (reset_seed_frame && pts <= reset_seed_pts + 1e-6) {
            av_frame_free(&clone);
            return false;
        }
        if (reset_seed_frame)
            av_frame_free(&reset_seed_frame);
        reset_seed_frame = clone;
        reset_seed_pts = pts;
        reset_seed_duration = duration;
        reset_seed_generation = generation;
    }

    return true;
}

void QmlMainWindow::snapshotPendingFrame()
{
    AVFrame *snapshot_frame = nullptr;
    double pts = 0.0;
    float duration = 0.0f;
    {
        QMutexLocker locker(&pending_frame_mutex);
        if (!pending_frame)
            return;
        snapshot_frame = av_frame_clone(pending_frame);
        if (!snapshot_frame)
            return;
        pts = pending_pts;
        duration = pending_duration;
    }
    snapshotLastFrame(snapshot_frame, pts, duration, true);
}

void QmlMainWindow::refreshPendingFrameAge()
{
    uint64_t stored_us = 0;
    {
        QMutexLocker locker(&pending_frame_mutex);
        if (!pending_frame) {
            updatePendingFrameAge(0.0);
            return;
        }
        stored_us = pending_frame_stored_us;
    }

    double age = 0.0;
    if (stored_us) {
        const uint64_t now_us = chiaki_time_now_monotonic_us();
        if (now_us > stored_us)
            age = static_cast<double>(now_us - stored_us) / 1000000.0;
    }

    updatePendingFrameAge(age);
}

void QmlMainWindow::snapshotLastFrame(AVFrame *frame, double pts, float duration, bool take_ownership)
{
    // qCInfo(chiakiGui) << "snapshotLastFrame invoked pts=" << pts << " duration=" << duration;
    bool stored = false;
    const char *reason = "unknown";
    auto log_result = [&]() {
        // qCInfo(chiakiGui) << "snapshotLastFrame result stored=" << stored << " reason=" << reason;
    };
    auto release_owned_frame = [&]() {
        if (take_ownership && frame)
            av_frame_free(&frame);
    };

    if (!frame) {
        reason = "null frame";
        log_result();
        return;
    }
    const bool is_hw = frame->hw_frames_ctx != nullptr;
    if (!is_hw && !frame->data[0]) {
        reason = "missing data";
        release_owned_frame();
        log_result();
        return;
    }
    if (!is_hw && av_pix_fmt_count_planes(static_cast<AVPixelFormat>(frame->format)) <= 0) {
        reason = "no planes";
        release_owned_frame();
        log_result();
        return;
    }

    AVFrame *clone = take_ownership ? frame : av_frame_clone(frame);
    if (!clone) {
        reason = "clone failure";
        release_owned_frame();
        log_result();
        return;
    }

    if (!take_ownership) {
        clone->format = frame->format;
        clone->width = frame->width;
        clone->height = frame->height;
        clone->sample_aspect_ratio = frame->sample_aspect_ratio;
        clone->colorspace = frame->colorspace;
        clone->color_range = frame->color_range;
        clone->color_primaries = frame->color_primaries;
        clone->color_trc = frame->color_trc;
        av_frame_copy_props(clone, frame);
        if (!is_hw && clone->hw_frames_ctx) {
            av_buffer_unref(&clone->hw_frames_ctx);
            clone->hw_frames_ctx = nullptr;
        }
    }
    clone->opaque = this;
    AVFrame *fallback_clone = make_fallback_snapshot_frame(frame);
    {
        QMutexLocker locker(&kept_frame_mutex);
        if (kept_frame)
            av_frame_free(&kept_frame);
        if (fallback_frame)
            av_frame_free(&fallback_frame);
        kept_frame = clone;
        kept_frame_pts = pts;
        kept_frame_duration = duration;
        stored = true;
        reason = "stored";
        fallback_frame = fallback_clone;
        if (!fallback_clone && frame->hw_frames_ctx && frame->format != AV_PIX_FMT_VULKAN)
            qCWarning(chiakiGui) << "snapshotLastFrame: failed to create fallback frame";
    }
    snapshot_generation.fetchAndAddRelaxed(1);
    // qCInfo(chiakiGui) << "snapshotLastFrame: stored kept_frame pts=" << pts << " duration=" << duration;
    log_result();
}

void QmlMainWindow::clearSnapshotFrame()
{
    QMutexLocker locker(&kept_frame_mutex);
    if (kept_frame)
        av_frame_free(&kept_frame);
    if (fallback_frame)
        av_frame_free(&fallback_frame);
    kept_frame = nullptr;
    fallback_frame = nullptr;
}

bool QmlMainWindow::enqueueKeptFrame(double queue_pts_origin_hint, bool deinterlace_enabled, double &used_origin)
{
    AVFrame *clone = nullptr;
    double pts = 0.0;
    float duration = 0.0f;
    {
        QMutexLocker locker(&kept_frame_mutex);
        AVFrame *source = fallback_frame ? fallback_frame : kept_frame;
        if (!source)
            return false;
        clone = clone_frame_for_replay(source);
        if (!clone)
            return false;
        pts = kept_frame_pts;
        duration = kept_frame_duration;
    }

    clone->opaque = this;
    double origin = queue_pts_origin_hint;
    if (origin < 0.0)
        origin = pts;
    used_origin = origin;

    struct pl_source_frame snapshot_src = {};
    snapshot_src.pts = pts - origin;
    snapshot_src.duration = static_cast<float>(duration);
    snapshot_src.first_field = deinterlace_enabled
        ? pl_field_from_avframe(clone)
        : PL_FIELD_NONE;
    snapshot_src.frame_data = clone;
    snapshot_src.map = map_frame;
    snapshot_src.unmap = unmap_frame;
    snapshot_src.discard = discard_snapshot_frame;

    {
        const qint64 push_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        pl_queue_push(placebo_queue, &snapshot_src);
        const qint64 push_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        logLatencyStats("pl_queue_push", push_end_us - push_begin_us);
        qCInfo(chiakiGui).nospace()
            << "[latency] pl_queue_push_snapshot_us=" << (push_end_us - push_begin_us)
            << " queue_depth=" << pl_queue_num_frames(placebo_queue);
    }
    newest_queued_frame_pts = qMax(newest_queued_frame_pts, pts);
    return true;
}

bool QmlMainWindow::queueStoredFrame(AVFrame *frame, double pts, float duration,
                                     void (*discard_cb)(const struct pl_source_frame *))
{
    if (!frame)
        return false;

    frame->opaque = this;

    if (duration > 0.0f)
        source_frame_interval_ms = 1000.0 * duration;
    bool deinterlace_enabled = false;
    struct pl_source_frame src_frame;
    {
        QMutexLocker locker(&placebo_state_mutex);
        if (newest_queued_frame_pts >= 0.0 && pts <= newest_queued_frame_pts + 1e-6) {
            logDroppedFrameReason("stale_stored_frame", static_cast<qint64>(chiaki_time_now_monotonic_us()), pts,
                                  "before queue submission");
            qCDebug(chiakiGui)
                << "Dropping stale stored frame before queue submission"
                << "frame_pts=" << pts
                << "newest_queued_pts=" << newest_queued_frame_pts;
            av_frame_free(&frame);
            return false;
        }
        const bool preserve_timeline = preserve_playback_timeline;
        preserve_playback_timeline = false;
        if (queue_pts_origin < 0.0 || pts + 1e-6 < queue_pts_origin) {
            logPtsRollback("applyPendingFrame/rollback", pts, queue_pts_origin, preserve_timeline, duration * 1000.0);
            pl_queue_reset(placebo_queue);
            const double old_origin = queue_pts_origin;
            queue_pts_origin = pts;
            newest_queued_frame_pts = -1.0;
            if (preserve_timeline && old_origin >= 0.0 && playback_started) {
                const double delta_pts = queue_pts_origin - old_origin;
                const int64_t delta_us = static_cast<int64_t>(delta_pts * 1000000.0);
                int64_t ts_start_signed = static_cast<int64_t>(ts_start) + delta_us;
                if (ts_start_signed < 0)
                    ts_start_signed = 0;
                ts_start = static_cast<uint64_t>(ts_start_signed);
            } else {
                ts_start = 0;
                playback_started = false;
            }
        }
        deinterlace_enabled = this->renderparams_opts->params.deinterlace_params;
        src_frame = {
            .pts = pts - queue_pts_origin,
            .duration = static_cast<float>(duration),
            .first_field = deinterlace_enabled
                ? pl_field_from_avframe(frame)
                : PL_FIELD_NONE,
            .frame_data = frame,
            .map = map_frame,
            .unmap = unmap_frame,
            .discard = discard_cb,
        };
        {
            const qint64 push_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            pl_queue_push(placebo_queue, &src_frame);
            const qint64 push_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            logLatencyStats("pl_queue_push", push_end_us - push_begin_us);
            qCInfo(chiakiGui).nospace()
                << "[latency] pl_queue_push_stored_us=" << (push_end_us - push_begin_us)
                << " queue_depth=" << pl_queue_num_frames(placebo_queue);
        }
        newest_queued_frame_pts = qMax(newest_queued_frame_pts, pts);
    }

    uint64_t now_us = chiaki_time_now_monotonic_us();
    uint64_t frame_pts_us = src_frame.pts > 0.0
        ? static_cast<uint64_t>(src_frame.pts * 1000000.0)
        : 0;
    if (!playback_started) {
        ts_start = frame_pts_us < now_us ? now_us - frame_pts_us : 0;
        playback_started = true;
    } else if (ts_start) {
        const double current_pts = static_cast<double>(now_us - ts_start) / 1000000.0;
        const double catch_up_threshold = qMax(static_cast<double>(duration) * 2.0, 0.05);
        if (src_frame.pts > current_pts + catch_up_threshold)
            ts_start = frame_pts_us < now_us ? now_us - frame_pts_us : 0;
    }

    if (!has_video) {
        has_video = true;
        if (!grab_input && settings->GetHideCursor())
            setCursor(Qt::BlankCursor);
        emit hasVideoChanged();
    }
    if (stream_session_active.loadAcquire() != 0 && configuredFrameMixerEnabledForScheduling())
        scheduleBufferedUpdate(UpdateRequestReason::QueueStoredFrame);
    else
        scheduleUpdate(false, UpdateRequestReason::QueueStoredFrame);
    return true;
}

bool QmlMainWindow::applyResetSeedFrame(quint64 generation)
{
    AVFrame *frame = nullptr;
    double pts = 0.0;
    float duration = 0.0f;
    {
        QMutexLocker locker(&reset_seed_mutex);
        if (!reset_seed_frame)
            return false;
        if (reset_seed_generation != generation) {
            av_frame_free(&reset_seed_frame);
            reset_seed_pts = 0.0;
            reset_seed_duration = 0.0f;
            reset_seed_generation = 0;
            return false;
        }
        frame = reset_seed_frame;
        pts = reset_seed_pts;
        duration = reset_seed_duration;
        reset_seed_frame = nullptr;
        reset_seed_pts = 0.0;
        reset_seed_duration = 0.0f;
        reset_seed_generation = 0;
    }

    return queueStoredFrame(frame, pts, duration, discard_frame);
}

bool QmlMainWindow::applyKeptFrameSnapshot()
{
    AVFrame *clone = nullptr;
    double pts = 0.0;
    float duration = 0.0f;
    {
        QMutexLocker locker(&kept_frame_mutex);
        AVFrame *source = fallback_frame ? fallback_frame : kept_frame;
        if (!source)
            return false;
        clone = clone_frame_for_replay(source);
        if (!clone)
            return false;
        pts = kept_frame_pts;
        duration = kept_frame_duration;
    }

    return queueStoredFrame(clone, pts, duration, discard_snapshot_frame);
}

void QmlMainWindow::applyPendingFrame()
{
    AVFrame *frame = nullptr;
    double pts = 0.0;
    float duration = 0.0f;
    {
        QMutexLocker locker(&pending_frame_mutex);
        if (!pending_frame)
        {
            updatePendingFrameAge(0.0);
            return;
        }
        frame = pending_frame;
        pts = pending_pts;
        duration = pending_duration;
        pending_frame = nullptr;
        pending_pts = 0.0;
        pending_duration = 0.0f;
        pending_frame_stored_us = 0;
        pending_frame_queue_origin = 0.0;
        pending_frame_present.storeRelease(0);
    }

    updatePendingFrameAge(0.0);
    const qint64 consumed_idle_us = last_render_idle_us.loadAcquire();
    const qint64 consumed_now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    const qint64 consumed_idle_gap_us = consumed_idle_us > 0 ? qMax<qint64>(0, consumed_now_us - consumed_idle_us) : -1;
    logPendingFrameLifecycle("consumed",
                             pts,
                             duration,
                             0,
                             consumed_idle_gap_us,
                             -1,
                             -1,
                             render_active.loadAcquire() != 0,
                             true);
    queueStoredFrame(frame, pts, duration, discard_frame);
}

bool QmlMainWindow::applyPendingFrameIfQueueHasCapacity()
{
    if (!hasPendingFrame())
        return false;
    const int depth_limit = effectiveQueueDepthLimit();
    const bool render_active_now = render_active.loadAcquire() != 0;
    bool render_scheduled_now = false;
    bool render_pending_now = false;
    {
        QMutexLocker locker(&render_schedule_mutex);
        render_scheduled_now = render_scheduled;
        render_pending_now = render_pending;
    }
    if (render_active_now) {
        logPendingFrameGate("deferred",
                            "render_busy",
                            0.0,
                            -1,
                            -1,
                            depth_limit,
                            render_active_now,
                            render_scheduled_now,
                            render_pending_now,
                            true,
                            false);
        return false;
    }

    int queue_depth = -1;
    bool has_capacity = false;
    {
        QMutexLocker locker(&placebo_state_mutex);
        queue_depth = pl_queue_num_frames(placebo_queue);
        has_capacity = queue_depth < depth_limit;
    }
    logPendingQueueDepthHold("applyPendingFrameIfQueueHasCapacity",
                             static_cast<qint64>(chiaki_time_now_monotonic_us()),
                             queue_depth,
                             depth_limit,
                             true,
                             render_active_now,
                             render_scheduled_now,
                             render_pending_now);
    if (has_capacity) {
        applyPendingFrame();
        return true;
    }
    qint64 deferred_age_us = -1;
    double deferred_pts = 0.0;
    {
        QMutexLocker locker(&pending_frame_mutex);
        deferred_pts = pending_pts;
        deferred_age_us = pending_frame_stored_us > 0
            ? static_cast<qint64>(chiaki_time_now_monotonic_us()) - static_cast<qint64>(pending_frame_stored_us)
            : -1;
    }
    logPendingFrameGate("deferred",
                        "queue_full",
                        deferred_pts,
                        deferred_age_us,
                        queue_depth,
                        depth_limit,
                        render_active_now,
                        render_scheduled_now,
                        render_pending_now,
                        true,
                        false);
    {
        QMutexLocker locker(&pending_frame_mutex);
        const qint64 deferred_idle_us = last_render_idle_us.loadAcquire();
        const qint64 deferred_now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        logPendingFrameLifecycle("deferred",
                                 pending_pts,
                                 pending_duration,
                                 pending_frame_stored_us > 0
                                     ? static_cast<qint64>(chiaki_time_now_monotonic_us()) - static_cast<qint64>(pending_frame_stored_us)
                                     : -1,
                                 deferred_idle_us > 0 ? qMax<qint64>(0, deferred_now_us - deferred_idle_us) : -1,
                                 queue_depth,
                                 depth_limit,
                                 render_active.loadAcquire() != 0,
                                 false);
    }
    return false;
}

bool QmlMainWindow::hasPendingFrame() const
{
    return pending_frame_present.loadAcquire() != 0;
}

int QmlMainWindow::effectiveQueueDepthLimit() const
{
    int configured_limit = settings ? settings->GetQueueDepthLimit() : kDefaultQueueDepthLimit;

    // Test hook: allow forcing queue depth via env var for quick A/B experiments.
    const QByteArray force_depth = qgetenv("CHIAKI_TEST_QUEUE_DEPTH");
    if (!force_depth.isEmpty()) {
        bool ok = false;
        const int v = force_depth.toInt(&ok);
        if (ok && v > 0)
            configured_limit = v;
    }
    int depth_limit = qMax(configured_limit, 2);

    const bool backlog_pending = hasPendingFrame();
    const double avg_depth = queue_depth_average;
    if (backlog_pending || avg_depth >= static_cast<double>(depth_limit) - 0.25)
        depth_limit += kAdaptiveQueueDepthBoost;

    return depth_limit;
}

bool QmlMainWindow::traceVaapiRenderPath() const
{
    return settings && settings->GetHardwareDecoder() == "vaapi";
}

AVBufferRef *QmlMainWindow::vulkanHwDeviceCtx()
{
    if (render_backend != RenderBackend::Vulkan)
        return nullptr;

    if (vulkan_hw_dev_ctx || vk_decode_queue_index < 0)
        return vulkan_hw_dev_ctx;

    // Check if we're on a broken Mesa version in a Van Gogh GPU, if so, fail early.
    // 0x163F is AMD Custom GPU 0405 (AMD Aerith (Steam Deck LCD) or Aerith Plus (Xbox ROG Ally) )
    // 0x1435 is AMD Custom GPU 0932 (AMD Sephiroth (Steam Deck OLED))
    // Both are Van Gogh GPUs and have broken Vulkan HW decode on radv between 24.1.0-24.1.2
    // Sadly, SteamOS marks all of their Mesa builds as patch version .99, which makes it impossible to tell if we're on a
    // fixed 24.1 driver, so let's require a 24.2 driver or newer on Van Gogh devices just to be safe.
    bool is_van_gogh = amd_card && (vk_device_props.deviceID == 0x163F || vk_device_props.deviceID == 0x1435);
    if (is_van_gogh && vk_device_driver_props.driverID == VK_DRIVER_ID_MESA_RADV && vk_device_props.driverVersion < VK_MAKE_VERSION(24, 2, 0)){
        qCWarning(chiakiGui) << "Refusing to create Vulkan decode context due to us running on a Van Gogh GPU with a broken radv version ";
        return nullptr;
    }

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
        format.setSwapInterval(settings->GetVSyncEnabled() ? 1 : 0);
        present_vsync_enabled = settings->GetVSyncEnabled();
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
        GET_PROC(vkGetPhysicalDeviceProperties2)
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
        // device driver properties is core since vulkan 1.2 and libplacebo requires 1.2+ so if device is created, it's safe to assume this exists
        VkPhysicalDeviceDriverProperties driver_props = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
        };
        VkPhysicalDeviceProperties2 device_props = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &driver_props,
        };
        vk_funcs.vkGetPhysicalDeviceProperties2(placebo_vulkan->phys_device, &device_props);
        vk_device_props = device_props.properties;
        vk_device_driver_props = driver_props;
        if(vk_device_props.vendorID == 0x1002)
        {
            amd_card = true;
            qCInfo(chiakiGui) << "Using amd graphics card";
        }
        else if(vk_device_props.vendorID == 0x10de)
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
        if (!quick_sem)
            qCCritical(chiakiGui) << "Failed to create quick Vulkan semaphore";
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

    buffered_pace_thread = new BufferedPlaybackPacerThread(this);
    buffered_pace_thread->start();

    present_pace_thread = new DeferredPresentPacerThread(this);
    present_pace_thread->start();

    deferred_swap_thread = new DeferredSwapThread(this);
    deferred_swap_thread->start();

    qml_engine = new QQmlEngine(this);
    qml_engine->addImageProvider(QStringLiteral("svg"), new QmlSvgProvider);
    if (!qml_engine->incubationController())
        qml_engine->setIncubationController(quick_window->incubationController());
    connect(qml_engine, &QQmlEngine::quit, this, &QWindow::close);

    backend = new QmlBackend(settings, this);
    connect(backend, &QmlBackend::sessionChanged, this, [this, exit_app_on_stream_exit](StreamSession *s) {
        session = s;
        stream_session_active.storeRelease(s ? 1 : 0);
        grab_input = 0;
        if (session)
            session->BlockInput(0);
        drainRenderThread();
        {
            QMutexLocker locker(&placebo_state_mutex);
            pl_queue_reset(placebo_queue);
            newest_queued_frame_pts = -1.0;
        }
        {
            QMutexLocker locker(&reset_seed_mutex);
            if (reset_seed_frame)
                av_frame_free(&reset_seed_frame);
            reset_seed_pts = 0.0;
            reset_seed_duration = 0.0f;
            reset_seed_generation = 0;
        }
        clearSnapshotFrame();
        snapshot_generation.storeRelaxed(0);
        pending_reset_snapshot_generation.storeRelaxed(0);
        last_reset_snapshot_generation.storeRelaxed(0);
        next_buffered_update_us = 0;
        last_buffered_interval_us = 0;
        buffered_timer_fired.storeRelease(0);
        queue_pts_origin = -1.0;
        newest_queued_frame_pts = -1.0;
        reset_seed_capture_active.storeRelease(0);
        reset_seed_capture_generation.storeRelease(0);
        playback_started = false;
        ts_start = 0;
        quick_begin_wait_last_us = 0;
        quick_render_skip_next = 0;
        queue_stored_frame_pending.storeRelease(0);
        if (buffered_pace_thread)
            buffered_pace_thread->disarm();
        present_pace_timer_rearm.storeRelease(0);
        if (present_pace_thread)
            present_pace_thread->disarm();
        last_swap_return_us.storeRelease(0);
        schedule_frame_mixer_active.storeRelaxed(configuredFrameMixerEnabledForScheduling() ? 1 : 0);
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
            setStreamMaxFPS(60);
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
        quick_need_sync.storeRelaxed(1);
        bool render_busy_now = false;
        {
            QMutexLocker locker(&render_schedule_mutex);
            render_busy_now = render_scheduled;
        }
        if (!render_busy_now)
            scheduleUpdate(false, UpdateRequestReason::SceneChanged);
    });
    connect(quick_render, &QQuickRenderControl::renderRequested, this, [this]() {
        quick_need_render.storeRelaxed(1);
        bool render_busy_now = false;
        {
            QMutexLocker locker(&render_schedule_mutex);
            render_busy_now = render_scheduled;
        }
        if (!render_busy_now)
            scheduleUpdate(false, UpdateRequestReason::RenderRequested);
    });


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

void QmlMainWindow::drainRenderThread()
{
    drainDeferredSwaps();

    if (!quick_render)
        return;

    if (quick_render->thread() == QThread::currentThread())
        return;

    // Wait for queued render work to finish before tearing down libplacebo stuff which can cause crash on closing stream
    QMetaObject::invokeMethod(quick_render, []() {}, Qt::BlockingQueuedConnection);
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
            render_pending_during_cycle.fetchAndAddRelaxed(1);
            const int reason = last_update_request_reason.loadAcquire();
            const bool render_active_now = render_active.loadAcquire() != 0;
            const int pending_during_cycle = render_pending_during_cycle.loadAcquire();
            const qint64 last_swap_us = last_swap_return_us.loadAcquire();
            const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            const qint64 swap_age_us = last_swap_us > 0 ? qMax<qint64>(0, now_us - last_swap_us) : -1;
            qCInfo(chiakiGui).nospace()
                << "[latency] render_pending reason="
                << updateRequestReasonName(reason)
                << " count=1";
            logRenderPendingStats(now_us,
                                  updateRequestReasonName(reason),
                                  render_active_now,
                                  pending_during_cycle,
                                  swap_age_us);
            return;
        }
        render_scheduled = true;
    }

    if (quick_need_sync.fetchAndStoreRelaxed(0) != 0) {
        quick_render->polishItems();
        if (quick_render->thread() == QThread::currentThread())
            sync();
        else
            QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::sync, this), Qt::BlockingQueuedConnection);
    }

    last_render_dispatch_us.storeRelease(static_cast<qint64>(chiaki_time_now_monotonic_us()));
    QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::render, this));
}

void QmlMainWindow::scheduleUpdate(bool force)
{
    scheduleUpdate(force, UpdateRequestReason::Unknown);
}

void QmlMainWindow::scheduleUpdate(bool force, UpdateRequestReason reason)
{
    if (QThread::currentThread() != QGuiApplication::instance()->thread()) {
        QMetaObject::invokeMethod(this, [this, force, reason]() { scheduleUpdate(force, reason); }, Qt::QueuedConnection);
        return;
    }

    if (!force) {
        if (!update_pending.testAndSetRelaxed(0, 1))
            return;
    } else {
        update_pending.storeRelaxed(1);
    }
    last_update_request_reason.storeRelaxed(static_cast<int>(reason));
    last_schedule_update_us.storeRelease(static_cast<qint64>(chiaki_time_now_monotonic_us()));
    QMetaObject::invokeMethod(this, [this]() {
        update_pending.storeRelaxed(0);
        last_update_us = chiaki_time_now_monotonic_us();
        const qint64 schedule_us = last_schedule_update_us.loadAcquire();
        if (schedule_us > 0 && last_update_us >= schedule_us)
            logScheduleToUpdateStats(last_update_us, last_update_us - schedule_us);
        update();
    }, Qt::QueuedConnection);
}

void QmlMainWindow::scheduleBufferedUpdate()
{
    scheduleBufferedUpdate(UpdateRequestReason::Unknown);
}

void QmlMainWindow::scheduleBufferedUpdate(UpdateRequestReason reason)
{
    if (QThread::currentThread() != QGuiApplication::instance()->thread()) {
        QMetaObject::invokeMethod(this, [this, reason]() { scheduleBufferedUpdate(reason); }, Qt::QueuedConnection);
        return;
    }
    if (!buffered_pace_thread)
        return;
    const bool stream_owned_playback = stream_session_active.loadAcquire() != 0;
    const bool timer_owned_playback = shouldUseRefreshPacedBuffering(
        stream_owned_playback,
        present_vsync_enabled,
        schedule_frame_mixer_active.loadAcquire());
    const bool render_busy_now = render_active.loadAcquire() != 0;
    auto hasBufferedWork = [this, stream_owned_playback]() {
        if (hasPendingFrame())
            return true;
        {
            QMutexLocker locker(&placebo_state_mutex);
            if (placebo_queue && pl_queue_num_frames(placebo_queue) > 0)
                return true;
        }
        if (stream_owned_playback &&
            (quick_need_sync.loadRelaxed() != 0 || quick_need_render.loadRelaxed() != 0))
            return true;
        if (placebo_reset_pending.loadAcquire() != 0)
            return true;
        return false;
    };
    double interval_ms = stream_configured_frame_interval_ms;
    if (timer_owned_playback) {
        double refresh_rate = screen() ? screen()->refreshRate() : 60.0;
        if (refresh_rate <= 1.0)
            refresh_rate = 60.0;
        interval_ms = 1000.0 / refresh_rate;
    }
    const qint64 interval_us = static_cast<qint64>(qMax(1.0, interval_ms) * 1000.0);
    const qint64 now_us = chiaki_time_now_monotonic_us();
    const qint64 last_swap_return_us = this->last_swap_return_us.loadAcquire();
    const qint64 start_frame_block_estimate_us = this->start_frame_block_estimate_us.loadAcquire();
    if (last_buffered_interval_us != interval_us) {
        next_buffered_update_us = 0;
        last_buffered_interval_us = interval_us;
    }
    if (next_buffered_update_us == 0) {
        const qint64 anchor_us = last_swap_return_us > 0 ? last_swap_return_us : now_us;
        next_buffered_update_us = anchor_us + interval_us;
    }
    const bool pace_thread_active = timer_owned_playback && buffered_pace_thread->isArmed();
    if (timer_owned_playback && pace_thread_active)
        return;
    if (timer_owned_playback && render_busy_now) {
        const qint64 next_wait_us = qMax<qint64>(1, next_buffered_update_us - now_us);
        logBufferedPacingStats(now_us, interval_us, next_wait_us, timer_owned_playback, pace_thread_active);
        buffered_pace_thread->arm(next_buffered_update_us);
        return;
    }
    if (now_us >= next_buffered_update_us) {
        const qint64 elapsed_us = now_us - next_buffered_update_us;
        const qint64 missed_intervals = elapsed_us >= 0 ? (elapsed_us / interval_us) + 1 : 1;
        next_buffered_update_us += missed_intervals * interval_us;
        logBufferedPacingStats(now_us, interval_us, -elapsed_us, timer_owned_playback, pace_thread_active);
        if (timer_owned_playback) {
            if (reason == UpdateRequestReason::Timer && !hasBufferedWork()) {
                buffered_pace_thread->arm(next_buffered_update_us);
                return;
            }
            buffered_pace_thread->arm(next_buffered_update_us);
            if (buffered_timer_fired.fetchAndStoreRelaxed(0) == 0)
                return;
        }
        scheduleUpdate(false, reason);
        return;
    }
    qint64 wait_us = next_buffered_update_us - now_us;
    logBufferedPacingStats(now_us, interval_us, wait_us, timer_owned_playback, pace_thread_active);
    buffered_pace_thread->arm(next_buffered_update_us);
}

void QmlMainWindow::handleBufferedPlaybackWake(qint64 timer_fire_us)
{
    logTimerFireInterval(timer_fire_us);
    buffered_timer_fired.storeRelease(1);
    scheduleBufferedUpdate(UpdateRequestReason::Timer);
}

bool QmlMainWindow::throttleFramePresentation(double interval_s)
{
    if (interval_s <= 0.0) {
        next_frame_target_us = 0;
        last_throttle_interval_s = 0.0;
        present_pace_timer_rearm.storeRelease(0);
        if (present_pace_thread)
            present_pace_thread->disarm();
        return true;
    }
    const uint64_t interval_us = static_cast<uint64_t>(interval_s * 1000000.0 + 0.5);
    if (!interval_us)
        return true;
    if (qAbs(last_throttle_interval_s - interval_s) > 1e-6)
        next_frame_target_us = 0;
    last_throttle_interval_s = interval_s;

    const uint64_t now_us = chiaki_time_now_monotonic_us();
    if (!next_frame_target_us)
        next_frame_target_us = now_us + interval_us;

    while (next_frame_target_us <= now_us)
        next_frame_target_us += interval_us;

    const qint64 start_frame_block_estimate_us = this->start_frame_block_estimate_us.loadAcquire();
    const uint64_t base_lead_us = static_cast<uint64_t>(qBound<qint64>(0, start_frame_block_estimate_us, static_cast<qint64>(interval_us / 2)));
    const qint64 extra_lead_us = 0;
    const uint64_t total_lead_us = base_lead_us;
    const uint64_t target_wakeup_us = next_frame_target_us > total_lead_us ? (next_frame_target_us - total_lead_us) : next_frame_target_us;
    const uint64_t remaining_to_target_us = next_frame_target_us > now_us ? (next_frame_target_us - now_us) : 0;
    logDeferredPresentDecision(static_cast<qint64>(now_us),
                               static_cast<qint64>(next_frame_target_us),
                               static_cast<qint64>(target_wakeup_us),
                               static_cast<qint64>(remaining_to_target_us),
                               start_frame_block_estimate_us,
                               static_cast<qint64>(total_lead_us),
                               extra_lead_us,
                               target_wakeup_us > now_us);

    const uint64_t wait_us = target_wakeup_us > now_us ? (target_wakeup_us - now_us) : 0;
    if (wait_us > 0) {
        present_pace_timer_rearm.storeRelease(1);
        last_present_target_us.storeRelease(static_cast<qint64>(next_frame_target_us));
        last_present_wakeup_target_us.storeRelease(static_cast<qint64>(target_wakeup_us));
        if (present_pace_thread)
            present_pace_thread->arm(static_cast<qint64>(target_wakeup_us));
        return false;
    }
    next_frame_target_us += interval_us;
    present_pace_timer_rearm.storeRelease(0);
    last_present_target_us.storeRelease(0);
    last_present_wakeup_target_us.storeRelease(0);
    if (present_pace_thread)
        present_pace_thread->disarm();
    return true;
}

void QmlMainWindow::handleDeferredPresentWake(qint64 timer_fire_us)
{
    logTimerFireInterval(timer_fire_us);
    if (present_pace_timer_rearm.fetchAndStoreRelaxed(0) == 0)
        return;

    const qint64 target_wakeup_us = last_present_wakeup_target_us.loadAcquire();
    const qint64 target_us = last_present_target_us.loadAcquire();
    bool render_scheduled_now = false;
    {
        QMutexLocker locker(&render_schedule_mutex);
        render_scheduled_now = render_scheduled;
    }
    const bool render_active_now = render_active.loadAcquire() != 0;
    logDeferredTimerWakeup(timer_fire_us,
                           target_wakeup_us,
                           target_us,
                           0,
                           render_active_now,
                           render_scheduled_now);
    if (render_active_now || render_scheduled_now) {
        logDeferredPresentRetry(timer_fire_us, render_active_now, render_scheduled_now);
        present_pace_timer_rearm.storeRelease(1);
        return;
    }
    scheduleUpdate(false, UpdateRequestReason::VSync);
}

bool QmlMainWindow::enqueueDeferredSwap(qint64 submit_begin_us,
                                       qint64 submit_us,
                                       int queue_depth_at_submit,
                                       int depth_limit,
                                       bool pending_frame_waiting,
                                       qint64 present_interval_us,
                                       bool clamp_debug_enabled)
{
    if (!deferred_swap_thread || render_backend != RenderBackend::Vulkan || !placebo_swapchain)
        return false;

    const int async_swap_enabled = qEnvironmentVariableIntValue("CHIAKI_ASYNC_SWAP");
    if (async_swap_enabled != 1)
        return false;

    DeferredSwapTask task;
    task.submit_begin_us = submit_begin_us;
    task.submit_us = submit_us;
    task.queue_depth_at_submit = queue_depth_at_submit;
    task.depth_limit = depth_limit;
    task.pending_frame_waiting = pending_frame_waiting;
    task.present_interval_us = present_interval_us;
    task.clamp_debug_enabled = clamp_debug_enabled;
    return deferred_swap_thread->enqueue(task);
}

void QmlMainWindow::processDeferredSwapTask(qint64 submit_begin_us,
                                            qint64 submit_us,
                                            int queue_depth_at_submit,
                                            int depth_limit,
                                            bool pending_frame_waiting,
                                            qint64 present_interval_us,
                                            bool clamp_debug_enabled)
{
    Q_UNUSED(queue_depth_at_submit);
    Q_UNUSED(depth_limit);
    Q_UNUSED(pending_frame_waiting);
    Q_UNUSED(present_interval_us);

    if (!placebo_swapchain)
        return;

    const qint64 present_swap_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (present_swap_begin_us >= submit_us)
        logLatencyStats("submit_to_swap", present_swap_begin_us - submit_us);

    pl_swapchain_swap_buffers(placebo_swapchain);

    const qint64 swap_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    logSwapInterval(swap_us);
    this->last_swap_return_us.storeRelease(swap_us);
    this->last_present_complete_us.storeRelease(swap_us);

    if (swap_us >= present_swap_begin_us)
        logLatencyStats("swap_call", swap_us - present_swap_begin_us);

    if (clamp_debug_enabled) {
        qCInfo(chiakiGui).nospace()
            << "[clamp] submit_begin=" << submit_begin_us
            << " submit_end=" << submit_us
            << " swap_end=" << swap_us
            << " present_complete=" << swap_us;
    }

    const qint64 observed = swap_us - submit_begin_us;
    if (observed > 0) {
        const long long prev = g_submit_swap_est_us.load(std::memory_order_acquire);
        const long long next = static_cast<long long>(((prev * 7ll) + observed) / 8ll);
        g_submit_swap_est_us.store(next, std::memory_order_release);
    }
}

void QmlMainWindow::drainDeferredSwaps()
{
    if (deferred_swap_thread)
        deferred_swap_thread->drain();
}

void QmlMainWindow::updatePlacebo()
{
    renderparams_changed = true;
    schedule_frame_mixer_active.storeRelaxed(configuredFrameMixerEnabledForScheduling() ? 1 : 0);
}

void QmlMainWindow::updateVSync()
{
    if (render_backend != RenderBackend::Vulkan)
        return;
    swapchain_recreate_pending.storeRelaxed(1);
    scheduleUpdate(false, UpdateRequestReason::VSync);
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

    const int swapchain_depth = settings->GetVSyncEnabled() ? 2 : 1;
    const char *force_mailbox_env = getenv("CHIAKI_FORCE_MAILBOX");
    const bool force_mailbox = (force_mailbox_env && strcmp(force_mailbox_env, "1") == 0);
    struct pl_vulkan_swapchain_params swapchain_params = {
        .surface = surface,
        /* Prefer MAILBOX for vsynced playback (lower latency when supported).
         * When CHIAKI_FORCE_MAILBOX=1 is set, attempt MAILBOX and avoid
         * falling back to FIFO so we can test MAILBOX behavior. */
        .present_mode = (settings->GetVSyncEnabled() || force_mailbox) ? VK_PRESENT_MODE_MAILBOX_KHR
                                                                       : VK_PRESENT_MODE_IMMEDIATE_KHR,
        .swapchain_depth = swapchain_depth,
    };
    placebo_swapchain = pl_vulkan_create_swapchain(placebo_vulkan, &swapchain_params);
    present_vsync_enabled = swapchain_params.present_mode != VK_PRESENT_MODE_IMMEDIATE_KHR;
    if (!placebo_swapchain && settings->GetVSyncEnabled()) {
        if (force_mailbox) {
            qCWarning(chiakiGui) << "MAILBOX present mode unsupported and CHIAKI_FORCE_MAILBOX=1 set; not falling back";
        } else {
            qCWarning(chiakiGui) << "MAILBOX present mode unsupported, falling back to FIFO VSync";
            swapchain_params.present_mode = VK_PRESENT_MODE_FIFO_KHR;
            placebo_swapchain = pl_vulkan_create_swapchain(placebo_vulkan, &swapchain_params);
            present_vsync_enabled = true;
        }
    }
    if (!placebo_swapchain && !settings->GetVSyncEnabled()) {
        qCWarning(chiakiGui) << "Immediate present mode unsupported, falling back to FIFO VSync";
        swapchain_params.present_mode = VK_PRESENT_MODE_FIFO_KHR;
        swapchain_params.swapchain_depth = 1;
        placebo_swapchain = pl_vulkan_create_swapchain(placebo_vulkan, &swapchain_params);
        present_vsync_enabled = true;
    }
    qCInfo(chiakiGui).nospace()
        << "[present] swapchain_create vsync_setting=" << (settings->GetVSyncEnabled() ? 1 : 0)
        << " requested_mode=" << (settings->GetVSyncEnabled() ? "FIFO" : "IMMEDIATE")
        << " selected_mode="
        << (swapchain_params.present_mode == VK_PRESENT_MODE_FIFO_KHR ? "FIFO"
            : swapchain_params.present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE"
            : swapchain_params.present_mode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX"
            : "OTHER")
        << " swapchain_depth=" << swapchain_depth
        << " present_vsync_enabled=" << (present_vsync_enabled ? 1 : 0)
        << " created=" << (placebo_swapchain ? 1 : 0);
    if (!placebo_swapchain) {
        vk_funcs.vkDestroySurfaceKHR(placebo_vk_inst->instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
}

void QmlMainWindow::destroySwapchain()
{
    Q_ASSERT(QThread::currentThread() == render_thread);

    if (!placebo_swapchain)
        return;

    drainDeferredSwaps();
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
    const bool swapchain_ready = quick_tex &&
                                 (render_backend != RenderBackend::OpenGL || quick_fbo) &&
                                 (render_backend != RenderBackend::Vulkan || quick_vk_image);
    if (window_size == swapchain_size && swapchain_ready)
        return;

    QSize new_swapchain_size = window_size;
    pl_swapchain_resize(placebo_swapchain, &new_swapchain_size.rwidth(), &new_swapchain_size.rheight());

    if (quick_frame)
        endFrame();

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
    VkFormat vk_format = VK_FORMAT_UNDEFINED;
    pl_tex new_quick_tex = nullptr;
    if (!pl_tex_recreate(placebo_vulkan->gpu, &new_quick_tex, &tex_params)) {
        qCCritical(chiakiGui) << "Failed to create placebo texture";
        if (new_quick_tex)
            pl_tex_destroy(placebo_vulkan->gpu, &new_quick_tex);
        return;
    }
    VkImage new_quick_vk_image = pl_vulkan_unwrap(placebo_vulkan->gpu, new_quick_tex, &vk_format, nullptr);
    if (!new_quick_vk_image) {
        qCCritical(chiakiGui) << "Failed to unwrap placebo texture";
        pl_tex_destroy(placebo_vulkan->gpu, &new_quick_tex);
        return;
    }
    if (quick_tex)
        pl_tex_destroy(placebo_vulkan->gpu, &quick_tex);
    quick_tex = new_quick_tex;
    quick_vk_image = new_quick_vk_image;
    quick_vk_format = vk_format;
    quick_sem_value = 0;
    swapchain_size = new_swapchain_size;
    quick_window->setRenderTarget(QQuickRenderTarget());
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

    if (!quick_tex || !quick_sem || !quick_vk_image)
        return;
    quick_sem_value += 1;

    const qint64 quick_hold_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    struct pl_vulkan_hold_params hold_params = {
        .tex = quick_tex,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .qf = VK_QUEUE_FAMILY_IGNORED,
        .semaphore = {
            .sem = quick_sem,
            .value = quick_sem_value,
        }
    };
    pl_vulkan_hold_ex(placebo_vulkan->gpu, &hold_params);
    const qint64 quick_hold_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (quick_hold_after_us >= quick_hold_begin_us)
        logQuickBeginHoldStats(quick_hold_after_us, quick_hold_after_us - quick_hold_begin_us);

    const qint64 quick_wait_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    VkSemaphoreWaitInfo wait_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &quick_sem,
        .pValues = &quick_sem_value,
    };
    vk_funcs.vkWaitSemaphores(placebo_vulkan->device, &wait_info, UINT64_MAX);
    const qint64 quick_wait_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (quick_wait_after_us >= quick_wait_begin_us) {
        quick_begin_wait_last_us = quick_wait_after_us - quick_wait_begin_us;
        logQuickBeginWaitStats(quick_wait_after_us, quick_begin_wait_last_us);
    }

    quick_window->setRenderTarget(QQuickRenderTarget::fromVulkanImage(
        quick_vk_image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        quick_vk_format,
        swapchain_size));
    quick_frame = true;
    const qint64 quick_qt_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    quick_render->beginFrame();
    const qint64 quick_qt_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (quick_qt_after_us >= quick_qt_begin_us)
        logQuickBeginQtStats(quick_qt_after_us, quick_qt_after_us - quick_qt_begin_us);
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
    const qint64 quick_end_qt_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    quick_render->endFrame();
    const qint64 quick_end_qt_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (quick_end_qt_after_us >= quick_end_qt_begin_us)
        logQuickEndQtStats(quick_end_qt_after_us, quick_end_qt_after_us - quick_end_qt_begin_us);

    const qint64 quick_release_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    struct pl_vulkan_release_params release_params = {
        .tex = quick_tex,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .qf = VK_QUEUE_FAMILY_IGNORED,
    };
    pl_vulkan_release_ex(placebo_vulkan->gpu, &release_params);
    const qint64 quick_release_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (quick_release_after_us >= quick_release_begin_us)
        logQuickReleaseStats(quick_release_after_us, quick_release_after_us - quick_release_begin_us);
}

void QmlMainWindow::render()
{
    Q_ASSERT(QThread::currentThread() == render_thread);
    const qint64 render_entry_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    last_render_entry_us.storeRelease(render_entry_us);
    const bool trace_vaapi = traceVaapiRenderPath();
    render_pending_during_cycle.storeRelaxed(0);
    const qint64 schedule_us = last_schedule_update_us.loadAcquire();
    if (schedule_us > 0 && render_entry_us >= schedule_us)
        logLatencyStats("schedule_to_render", render_entry_us - schedule_us);
    const qint64 update_us = last_update_us;
    if (update_us > 0 && render_entry_us >= update_us)
        logUpdateToRenderStats(render_entry_us, render_entry_us - update_us);
    const qint64 dispatch_us = last_render_dispatch_us.loadAcquire();
    if (trace_vaapi && dispatch_us > 0 && render_entry_us >= dispatch_us)
        logLatencyStats("render_dispatch_to_entry", render_entry_us - dispatch_us);
    if (trace_vaapi) {
        const qint64 last_render_idle_us_local = last_render_idle_us.loadAcquire();
        if (last_render_idle_us_local > 0 && render_entry_us >= last_render_idle_us_local)
            logLatencyStats("render_idle_to_entry", render_entry_us - last_render_idle_us_local);
    }

    auto snapshotRenderScheduleState = [this]() {
        QMutexLocker locker(&render_schedule_mutex);
        return qMakePair(render_scheduled, render_pending);
    };
    auto snapshotQueueDepth = [this]() {
        QMutexLocker locker(&placebo_state_mutex);
        return pl_queue_num_frames(placebo_queue);
    };
    if (trace_vaapi) {
        const qint64 last_render_idle_us_local = last_render_idle_us.loadAcquire();
        const qint64 request_age_us = schedule_us > 0 ? qMax<qint64>(0, render_entry_us - schedule_us) : -1;
        const qint64 idle_age_us = last_render_idle_us_local > 0 ? qMax<qint64>(0, render_entry_us - last_render_idle_us_local) : -1;
        const auto render_schedule_state = snapshotRenderScheduleState();
        logRenderHandoffStats(render_entry_us,
                              idle_age_us,
                              request_age_us,
                              snapshotQueueDepth(),
                              hasPendingFrame(),
                              render_active.loadAcquire() != 0,
                              render_schedule_state.first,
                              render_schedule_state.second);
    }

    struct RenderActiveGuard {
        QmlMainWindow *self;
        QAtomicInteger<int> &flag;
        qint64 render_entry_us;
        RenderActiveGuard(QmlMainWindow *self, QAtomicInteger<int> &flag, qint64 render_entry_us)
            : self(self), flag(flag), render_entry_us(render_entry_us)
        {
            flag.storeRelease(1);
        }
        ~RenderActiveGuard()
        {
            flag.storeRelease(0);
            const qint64 render_idle_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            self->last_render_idle_us.storeRelease(render_idle_us);
            bool render_pending_now = false;
            bool pending_frame_waiting_now = false;
            int pending_during_cycle_now = 0;
            {
                QMutexLocker locker(&self->render_schedule_mutex);
                render_pending_now = self->render_pending;
                pending_during_cycle_now = self->render_pending_during_cycle.loadAcquire();
            }
            pending_frame_waiting_now = self->hasPendingFrame();
            if (render_idle_us >= render_entry_us)
                logRenderIdleStats(render_idle_us,
                                   render_idle_us - render_entry_us,
                                   render_pending_now,
                                   pending_frame_waiting_now,
                                   pending_during_cycle_now);
            if (pending_frame_waiting_now)
                self->applyPendingFrameIfQueueHasCapacity();
            if (self->present_pace_timer_rearm.fetchAndStoreRelaxed(0) != 0) {
                bool render_scheduled_now = false;
                {
                    QMutexLocker locker(&self->render_schedule_mutex);
                    render_scheduled_now = self->render_scheduled || self->render_pending;
                }
                if (render_scheduled_now)
                    self->present_pace_timer_rearm.storeRelease(1);
            }
        }
    } render_guard(this, render_active, render_entry_us);

    auto queueHasWork = [this]() {
        QMutexLocker locker(&placebo_state_mutex);
        return pl_queue_num_frames(placebo_queue) > 0;
    };

    auto hasBufferedWork = [this]() {
        {
            QMutexLocker locker(&placebo_state_mutex);
            if (pl_queue_num_frames(placebo_queue) > 0)
                return true;
        }
        if (hasPendingFrame())
            return true;
        if (placebo_reset_pending.loadAcquire() != 0)
            return true;
        return false;
    };

    auto finalize_render = [this]() {
        bool pending = false;
        {
            QMutexLocker locker(&render_schedule_mutex);
            render_scheduled = false;
            pending = render_pending;
            render_pending = false;
        }
        if (pending) {
            const int pending_during_cycle = render_pending_during_cycle.loadAcquire();
            if (pending_during_cycle > 0) {
                qCInfo(chiakiGui).nospace()
                    << "[latency] render_cycle_pending count=" << pending_during_cycle;
            }
            QMetaObject::invokeMethod(this, [this]() {
                const bool pending_stored_frame = queue_stored_frame_pending.fetchAndStoreRelaxed(0) != 0;
                if (!pending_stored_frame)
                    return;
                scheduleUpdate(false, UpdateRequestReason::FinalizePending);
            }, Qt::QueuedConnection);
        }
        return pending;
    };
    if (swapchain_recreate_pending.fetchAndStoreRelaxed(0)) {
        destroySwapchain();
        createSwapchain();
        resizeSwapchain();
    }

    if (placebo_reset_pending.fetchAndStoreRelaxed(0) != 0)
        resetPlaceboQueue();

    if (!placebo_swapchain)
    {
        bool scheduled_pending = finalize_render();
        if (!scheduled_pending && (queueHasWork() || placebo_reset_pending.loadAcquire() != 0))
            scheduleUpdate(false, UpdateRequestReason::NoSwapchain);
        return;
    }

    if (renderer_cache_flush_pending.fetchAndStoreRelaxed(0) && placebo_renderer)
        pl_renderer_flush_cache(placebo_renderer);

    const struct pl_render_params *render_params = &pl_render_default_params;
    const PlaceboUpscaler configured_upscaler = settings->GetPlaceboUpscaler();
    switch (video_preset) {
    case VideoPreset::Fast:
        render_params = &pl_render_fast_params;
        break;
    case VideoPreset::Default:
        render_params = &pl_render_default_params;
        if (render_params->deinterlace_params)
            this->renderparams_opts->params.deinterlace_params = render_params->deinterlace_params;
        break;
    case VideoPreset::HighQuality:
        render_params = &pl_render_high_quality_params;
        if (render_params->deinterlace_params)
            this->renderparams_opts->params.deinterlace_params = render_params->deinterlace_params;
        break;
    case VideoPreset::HighQualitySpatial:
        render_params = &pl_render_high_quality_params;
        if (render_params->deinterlace_params)
            this->renderparams_opts->params.deinterlace_params = render_params->deinterlace_params;
        break;
    case VideoPreset::HighQualityAdvancedSpatial:
        render_params = &pl_render_high_quality_params;
        if (render_params->deinterlace_params)
            this->renderparams_opts->params.deinterlace_params = render_params->deinterlace_params;
        break;
    case VideoPreset::Custom:
        if (renderparams_changed) {
            renderparams_changed = false;
            QMap<QString, QString> paramsData = settings->GetPlaceboValues();
            QMapIterator<QString, QString> i(paramsData);
            bool invalid_render_params = false;
            {
                QMutexLocker locker(&placebo_state_mutex);
                this->renderparams_opts->params = pl_render_default_params;
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
    params.frame_mixer = effectiveFrameMixerConfig(&params);
    const bool content_timed = params.frame_mixer == nullptr;
    const bool mixer_active = params.frame_mixer != nullptr;
    const bool has_settings = settings != nullptr;
    const bool apply_display_target = has_settings && stream_session_active.loadAcquire() != 0;
    const int target_trc = apply_display_target ? settings->GetDisplayTargetTrc() : 0;
    const int target_prim = apply_display_target ? settings->GetDisplayTargetPrim() : 0;
    const int target_peak = apply_display_target ? settings->GetDisplayTargetPeak() : 0;
    const int target_contrast = apply_display_target ? settings->GetDisplayTargetContrast() : 0;

    uint64_t ts_pre_update = chiaki_time_now_monotonic_us();
    double refresh_rate = screen() ? screen()->refreshRate() : 60.0;
    if (refresh_rate <= 1.0)
        refresh_rate = 60.0;
    const double refresh_interval_s = 1.0 / refresh_rate;
    const bool pending_frame_waiting = hasPendingFrame();
    const double stream_interval_s = stream_configured_frame_interval_ms > 0.0
        ? stream_configured_frame_interval_ms / 1000.0
        : 0.0;
    const double vsync_duration = refresh_interval_s;
    const double present_interval_s = refresh_interval_s;
    const qint64 present_interval_us = static_cast<qint64>(qMax(1.0, vsync_duration * 1000000.0) + 0.5);
    const bool timer_owned_playback = stream_session_active.loadAcquire() != 0 && mixer_active;
    auto schedulePostRenderWork = [this, &hasBufferedWork]() {
        if (!hasBufferedWork())
            return;
        scheduleUpdate(false, UpdateRequestReason::FinalizePending);
    };
    qparams.timeout = 0;
    schedule_frame_mixer_active.storeRelaxed(mixer_active ? 1 : 0);
    qparams.interpolation_threshold = mixer_active ? 0.01 : 0.0;
    qparams.radius = pl_frame_mix_radius(&params);
    qparams.vsync_duration = vsync_duration;
    qparams.pts = playback_started
        ? (double)(ts_pre_update - ts_start) / 1000000.0 + vsync_duration
        : 0.0;
    const int depth_limit = effectiveQueueDepthLimit();
    enum pl_queue_status queue_status;
    bool replay_attempted = false;
    bool replay_enqueued = false;
    const qint64 queue_update_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    int queue_depth_before_update = -1;
    {
        QMutexLocker locker(&placebo_state_mutex);
        queue_depth_before_update = pl_queue_num_frames(placebo_queue);
    }
    {
        QMutexLocker locker(&placebo_state_mutex);
        queue_status = pl_queue_update(placebo_queue, &frame_mix, &qparams);
        const bool pending_frame_blocked = pending_frame_waiting
            && pl_queue_num_frames(placebo_queue) >= depth_limit;
        if (frame_mix.num_frames == 0 && (!pending_frame_waiting || pending_frame_blocked)) {
            replay_attempted = true;
            double used_origin = -1.0;
            if (enqueueKeptFrame(queue_pts_origin, this->renderparams_opts->params.deinterlace_params, used_origin)) {
                replay_enqueued = true;
                queue_pts_origin = used_origin;
                if (!playback_started) {
                    uint64_t now_us = chiaki_time_now_monotonic_us();
                    ts_start = now_us;
                    playback_started = true;
                }
                queue_status = pl_queue_update(placebo_queue, &frame_mix, &qparams);
            }
        }
    }
    switch (queue_status) {
    case PL_QUEUE_ERR:
        finalize_render();
        return;
    case PL_QUEUE_EOF:
    case PL_QUEUE_OK:
    case PL_QUEUE_MORE:
        break;
    }
    if (applyPendingFrameIfQueueHasCapacity()) {
        {
            QMutexLocker locker(&placebo_state_mutex);
            queue_status = pl_queue_update(placebo_queue, &frame_mix, &qparams);
        }
        switch (queue_status) {
        case PL_QUEUE_ERR:
            finalize_render();
            return;
        case PL_QUEUE_EOF:
        case PL_QUEUE_OK:
        case PL_QUEUE_MORE:
            break;
        }
    }
    {
        const qint64 queue_update_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        if (queue_update_after_us >= queue_update_begin_us) {
            logRenderQueueUpdateStats(queue_update_after_us, queue_update_after_us - queue_update_begin_us);
            logRenderPhaseStall(queue_update_after_us,
                                "queue_update",
                                queue_update_after_us - queue_update_begin_us,
                                queue_depth_before_update,
                                depth_limit,
                                pending_frame_waiting,
                                render_active.loadAcquire() != 0,
                                snapshotRenderScheduleState().first,
                                snapshotRenderScheduleState().second);
        }
    }
    updateQueueDepthAverage(snapshotQueueDepth());
    refreshPendingFrameAge();

    if (render_backend == RenderBackend::OpenGL && quick_need_render.loadRelaxed()) {
        const bool quick_skip_next = stream_session_active.loadAcquire() != 0 && quick_render_skip_next != 0;
        if (quick_skip_next && !quick_frame) {
            quick_render_skip_next = 0;
            quick_need_render.storeRelaxed(1);
        } else {
            quick_need_render.storeRelaxed(0);
            const qint64 quick_begin_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (!quick_frame)
                beginFrame();
            const qint64 quick_begin_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (quick_begin_after_us >= quick_begin_begin_us)
                logQuickBeginFrameStats(quick_begin_after_us, quick_begin_after_us - quick_begin_begin_us);
            if (quick_frame) {
                const qint64 quick_render_call_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                clearQuickOpenGLTarget(quick_fbo);
                quick_render->render();
                const qint64 quick_render_call_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                if (quick_render_call_after_us >= quick_render_call_begin_us)
                    logQuickRenderStats(quick_render_call_after_us, quick_render_call_after_us - quick_render_call_begin_us);
            }
            const qint64 quick_end_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            endFrame();
            const qint64 quick_end_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (quick_end_after_us >= quick_end_begin_us)
                logQuickEndFrameStats(quick_end_after_us, quick_end_after_us - quick_end_begin_us);
            quick_render_skip_next = stream_session_active.loadAcquire() != 0 &&
                render_pending_during_cycle.loadAcquire() > 1 &&
                quick_begin_wait_last_us > 1500 ? 1 : 0;
        }
    }

    if (qEnvironmentVariableIntValue("CHIAKI_ASYNC_SWAP") == 1)
        drainDeferredSwaps();

    const qint64 start_frame_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    const int queue_depth_before_start = snapshotQueueDepth();
    const qint64 last_present_complete_us = this->last_present_complete_us.loadAcquire();
    if (last_present_complete_us > 0 && start_frame_begin_us >= last_present_complete_us)
        logSwapToStartGapStats(start_frame_begin_us, start_frame_begin_us - last_present_complete_us);
    struct pl_swapchain_frame sw_frame = {};
    if (!pl_swapchain_start_frame(placebo_swapchain, &sw_frame)) {
        qCWarning(chiakiGui) << "Failed to start Placebo frame!";
        finalize_render();
        return;
    }
    const qint64 start_frame_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (start_frame_us >= start_frame_begin_us)
    {
        const qint64 start_frame_block_us = start_frame_us - start_frame_begin_us;
        logLatencyStats("start_frame_block", start_frame_block_us);
        const auto render_schedule_state = snapshotRenderScheduleState();
        logRenderPhaseStall(start_frame_us,
                            "start_frame",
                            start_frame_block_us,
                            queue_depth_before_start,
                            depth_limit,
                            pending_frame_waiting,
                            render_active.loadAcquire() != 0,
                                render_schedule_state.first,
                                render_schedule_state.second);
        logPresentOutlier("start_frame_block",
                          start_frame_us,
                          start_frame_block_us,
                          false,
                          present_vsync_enabled,
                          present_interval_us,
                          queue_depth_before_start,
                          depth_limit,
                          pending_frame_waiting,
                          render_active.loadAcquire() != 0,
                          render_schedule_state.first,
                          render_schedule_state.second);
        const qint64 prior_estimate_us = start_frame_block_estimate_us.loadAcquire();
        const qint64 next_estimate_us = prior_estimate_us > 0
            ? ((prior_estimate_us * 7) + start_frame_block_us) / 8
            : start_frame_block_us;
        start_frame_block_estimate_us.storeRelease(next_estimate_us);
    }
    const qint64 render_to_start_frame_us = start_frame_begin_us;
    const qint64 render_entry_us_local = last_render_entry_us.loadAcquire();
    if (render_entry_us_local > 0 && render_to_start_frame_us >= render_entry_us_local)
        logLatencyStats("render_to_start_frame", render_to_start_frame_us - render_entry_us_local);
    const bool throttle_present_for_stream = stream_session_active.loadAcquire() != 0 &&
        !pending_frame_waiting &&
        queue_depth_before_start < depth_limit;
    auto close_started_frame = [this, timer_owned_playback, present_interval_us, start_frame_us, depth_limit, pending_frame_waiting, snapshotQueueDepth, snapshotRenderScheduleState, trace_vaapi, stream_interval_s, throttle_present_for_stream](bool present) -> bool {
        const qint64 submit_ready_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        /* One-time debug flag for clamp diagnostics within this lambda */
        const char *__chiaki_dbg_clamp_env = getenv("CHIAKI_DEBUG_CLAMP");
        const bool __chiaki_dbg_clamp = __chiaki_dbg_clamp_env && strcmp(__chiaki_dbg_clamp_env, "1") == 0;
        if (submit_ready_us >= start_frame_us)
            logLatencyStats("start_frame_to_submit", submit_ready_us - start_frame_us);
        const int queue_depth_now = snapshotQueueDepth();
        const int pending_during_cycle_now = render_pending_during_cycle.loadAcquire();
        const bool should_throttle_present_now = present &&
            throttle_present_for_stream &&
            !pending_frame_waiting &&
            pending_during_cycle_now == 0 &&
            queue_depth_now < depth_limit;
        if (present && !should_throttle_present_now)
            throttleFramePresentation(0.0);
        if (should_throttle_present_now) {
            const uint64_t interval_us = static_cast<uint64_t>(stream_interval_s * 1000000.0 + 0.5);
                if (interval_us) {
                if (qAbs(last_throttle_interval_s - stream_interval_s) > 1e-6)
                    next_frame_target_us = 0;
                last_throttle_interval_s = stream_interval_s;

                const uint64_t now_us = chiaki_time_now_monotonic_us();
                if (!next_frame_target_us)
                    next_frame_target_us = now_us + interval_us;
                while (next_frame_target_us <= now_us)
                    next_frame_target_us += interval_us;

                /* Use a local target_us for wake/sleep/clamp so we don't rely on
                 * modifying the shared `next_frame_target_us` until after submit. */
                const uint64_t target_us = next_frame_target_us;
                const uint64_t wait_us = target_us > now_us ? (target_us - now_us) : 0;
                if (wait_us > 0) {
                    const long long est = g_submit_swap_est_us.load(std::memory_order_acquire);
                    const long long spin_margin = g_submit_swap_spin_margin_us;
                    const qint64 now_us_local = static_cast<qint64>(chiaki_time_now_monotonic_us());
                    const qint64 wake_at = target_us > (est + spin_margin)
                        ? static_cast<qint64>(target_us - (est + spin_margin))
                        : static_cast<qint64>(target_us);
                    const uint64_t sleep_us = wait_us > static_cast<uint64_t>(qMax<long long>(0, est))
                        ? (wait_us - static_cast<uint64_t>(qMax<long long>(0, est))) : 0;
                    const uint64_t max_render_sleep_us = 1000;
                    /* Debug: print calculated clamp/wake values when enabled */
                    if (__chiaki_dbg_clamp) {
                        qCInfo(chiakiGui).nospace()
                            << "[clamp] pre-wake target_us=" << target_us
                            << " wait_us=" << wait_us
                            << " est=" << est
                            << " spin_margin=" << spin_margin
                            << " now=" << now_us_local
                            << " wake_at=" << wake_at
                            << " sleep_us=" << sleep_us;
                    }
                    if (sleep_us > max_render_sleep_us && present_pace_thread) {
                        present_pace_timer_rearm.storeRelease(1);
                        last_present_target_us.storeRelease(static_cast<qint64>(target_us));
                        last_present_wakeup_target_us.storeRelease(static_cast<qint64>(wake_at));
                        present_pace_thread->arm(static_cast<qint64>(wake_at));
                        QThread::usleep(static_cast<unsigned long>(qMin<uint64_t>(sleep_us, max_render_sleep_us)));
                    } else if (sleep_us > 0) {
                        QThread::usleep(static_cast<unsigned long>(sleep_us));
                    }
                }
                /* Do NOT advance `next_frame_target_us` here; advance after submit
                 * so the clamp logic below compares against the intended target_us. */
            }
        }
        /* Non-blocking pacing kept for reference:
         * if (should_throttle_present_now)
         *     throttleFramePresentation(stream_interval_s);
         */
        /* If we're pacing for the stream, ensure we don't submit earlier than the scheduled target. */
        if (should_throttle_present_now) {
            const qint64 now_for_clamp_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            const qint64 target_us = next_frame_target_us;
            /* Debug: log clamp target vs now when enabled */
            if (__chiaki_dbg_clamp) {
                qCInfo(chiakiGui).nospace()
                    << "[clamp] pre-clamp target_us=" << target_us
                    << " now=" << now_for_clamp_us
                    << " rem=" << (target_us > now_for_clamp_us ? (target_us - now_for_clamp_us) : 0);
            }
            if (target_us > now_for_clamp_us) {
                qint64 rem = target_us - now_for_clamp_us;
                if (rem > 2000)
                    QThread::usleep(static_cast<unsigned long>(rem - 1000));
                while (static_cast<qint64>(chiaki_time_now_monotonic_us()) < target_us) {
                    QThread::usleep(500);
                }
            }
        }
        const qint64 submit_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        if (!pl_swapchain_submit_frame(placebo_swapchain))
            qCWarning(chiakiGui) << "Failed to submit Placebo frame!";
        else {
            const qint64 submit_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            logLatencyStats("pl_swapchain_submit_frame", submit_us - submit_begin_us);
            qCInfo(chiakiGui).nospace()
                << "[latency] pl_swapchain_submit_frame_us=" << (submit_us - submit_begin_us)
                << " queue_depth=" << pl_queue_num_frames(placebo_queue);
            qint64 present_complete_us = submit_us;
            const auto submit_render_state = snapshotRenderScheduleState();
            if (submit_us >= submit_begin_us)
            {
                const qint64 submit_call_us = submit_us - submit_begin_us;
                logLatencyStats("submit_call", submit_call_us);
                logLatencyStats("submit_frame_block", submit_call_us);
                logPresentOutlier("submit_call",
                                  submit_us,
                                  submit_call_us,
                                  false,
                                  present_vsync_enabled,
                                  present_interval_us,
                                  queue_depth_now,
                                  depth_limit,
                                  pending_frame_waiting,
                                  render_active.loadAcquire() != 0,
                                  submit_render_state.first,
                                  submit_render_state.second);
            }
            logRenderPhaseStall(submit_us,
                                "submit",
                                submit_us - submit_begin_us,
                                snapshotQueueDepth(),
                                depth_limit,
                                pending_frame_waiting,
                                render_active.loadAcquire() != 0,
                                submit_render_state.first,
                                submit_render_state.second);
            logPresentSubmitStats(submit_us, present, timer_owned_playback, present_interval_us);
            if (present) {
                static qint64 last_present_submit_us = 0;
                if (last_present_submit_us != 0 && submit_us >= last_present_submit_us) {
                    const qint64 present_submit_interval_us = submit_us - last_present_submit_us;
                    logPresentOutlier("present_interval",
                                      submit_us,
                                      present_submit_interval_us,
                                      false,
                                      present_vsync_enabled,
                                      present_interval_us,
                                      queue_depth_now,
                                      depth_limit,
                                      pending_frame_waiting,
                                      render_active.loadAcquire() != 0,
                                      submit_render_state.first,
                                      submit_render_state.second);
                }
                last_present_submit_us = submit_us;
            }
            if (present) {
                const bool async_enqueued = enqueueDeferredSwap(submit_begin_us,
                                                                submit_us,
                                                                queue_depth_now,
                                                                depth_limit,
                                                                pending_frame_waiting,
                                                                present_interval_us,
                                                                __chiaki_dbg_clamp);
                if (async_enqueued) {
                    present_complete_us = submit_us;
                } else {
                    const qint64 present_swap_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                    if (present_swap_begin_us >= submit_us)
                        logLatencyStats("submit_to_swap", present_swap_begin_us - submit_us);
                    pl_swapchain_swap_buffers(placebo_swapchain);
                    const qint64 swap_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                    logSwapInterval(swap_us);
                    this->last_swap_return_us.storeRelease(swap_us);
                    present_complete_us = swap_us;
                    const auto swap_render_state = snapshotRenderScheduleState();
                    logRenderPhaseStall(swap_us,
                                        "swap",
                                        swap_us - present_swap_begin_us,
                                        snapshotQueueDepth(),
                                        depth_limit,
                                        pending_frame_waiting,
                                        render_active.loadAcquire() != 0,
                                        swap_render_state.first,
                                        swap_render_state.second);
                    if (swap_us >= present_swap_begin_us)
                    {
                        const qint64 swap_call_us = swap_us - present_swap_begin_us;
                        logLatencyStats("swap_call", swap_call_us);
                        logPresentOutlier("swap_call",
                                          swap_us,
                                          swap_call_us,
                                          false,
                                          present_vsync_enabled,
                                          present_interval_us,
                                          snapshotQueueDepth(),
                                          depth_limit,
                                          pending_frame_waiting,
                                          render_active.loadAcquire() != 0,
                                          swap_render_state.first,
                                          swap_render_state.second);
                    }
                    const qint64 render_us = last_render_entry_us.loadAcquire();
                    if (render_us > 0 && swap_us >= render_us)
                        logLatencyStats("render_to_swap", swap_us - render_us);
                    /* Debug: report submit/swap timestamps when clamp debug enabled */
                    if (__chiaki_dbg_clamp) {
                        qCInfo(chiakiGui).nospace()
                            << "[clamp] submit_begin=" << submit_begin_us
                            << " submit_end=" << submit_us
                            << " swap_end=" << swap_us
                            << " present_complete=" << present_complete_us;
                    }
                    /* Update exponential moving average of submit+swap cost to compensate throttle sleeps. */
                    {
                        const qint64 observed = swap_us - submit_begin_us;
                        if (observed > 0) {
                            const long long prev = g_submit_swap_est_us.load(std::memory_order_acquire);
                            const long long next = static_cast<long long>(((prev * 7ll) + observed) / 8ll);
                            g_submit_swap_est_us.store(next, std::memory_order_release);
                        }
                    }
                }
            }
            if (present)
                this->last_present_complete_us.storeRelease(present_complete_us);
        }
        return true;
    };

    const struct pl_frame *hint_frame = nullptr;
    for (int i = 0; i < frame_mix.num_frames; i++) {
        if (frame_mix.timestamps[i] > 0.0f)
            break;
        hint_frame = frame_mix.frames[i];
    }
    if (!hint_frame && frame_mix.num_frames)
        hint_frame = frame_mix.frames[0];
    struct pl_color_space hint = hint_frame ? hint_frame->color : pl_color_space{};
    struct pl_color_space target_csp = sw_frame.color_space;
    if (target_csp.hdr.max_luma <= 0.0f) {
        target_csp.hdr.max_luma = 0.0f;
        target_csp.hdr.min_luma = 0.0f;
    }

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
    const qint64 render_setup_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    const int queue_depth_before_render_setup = [&]() {
        QMutexLocker locker(&placebo_state_mutex);
        return pl_queue_num_frames(placebo_queue);
    }();
    pl_swapchain_colorspace_hint(placebo_swapchain, &hint);
    const qint64 render_setup_config_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (render_setup_config_after_us >= render_setup_begin_us) {
        const auto render_schedule_state = snapshotRenderScheduleState();
        logRenderPhaseStall(render_setup_config_after_us,
                            "render_setup_config",
                            render_setup_config_after_us - render_setup_begin_us,
                            queue_depth_before_render_setup,
                            depth_limit,
                            pending_frame_waiting,
                            render_active.loadAcquire() != 0,
                            render_schedule_state.first,
                            render_schedule_state.second);
    }

    struct pl_frame target_frame = {};
    pl_frame_from_swapchain(&target_frame, &sw_frame);
    if (target_frame.num_planes <= 0) {
        qCWarning(chiakiGui) << "Swapchain frame contains no planes, skipping render";
        close_started_frame(false);
        finalize_render();
        return;
    }
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
        const bool quick_skip_next = stream_session_active.loadAcquire() != 0 && quick_render_skip_next != 0;
        if (quick_skip_next && !quick_frame) {
            quick_render_skip_next = 0;
            quick_need_render.storeRelaxed(1);
        } else {
            quick_need_render.storeRelaxed(0);
            const qint64 quick_begin_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (!quick_frame)
                beginFrame();
            const qint64 quick_begin_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (quick_begin_after_us >= quick_begin_begin_us)
                logQuickBeginFrameStats(quick_begin_after_us, quick_begin_after_us - quick_begin_begin_us);
            if (quick_frame) {
                const qint64 quick_render_call_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                if (render_backend == RenderBackend::OpenGL)
                    clearQuickOpenGLTarget(quick_fbo);
                quick_render->render();
                const qint64 quick_render_call_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                if (quick_render_call_after_us >= quick_render_call_begin_us)
                    logQuickRenderStats(quick_render_call_after_us, quick_render_call_after_us - quick_render_call_begin_us);
            }
            const qint64 quick_end_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            endFrame();
            const qint64 quick_end_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (quick_end_after_us >= quick_end_begin_us)
                logQuickEndFrameStats(quick_end_after_us, quick_end_after_us - quick_end_begin_us);
            quick_render_skip_next = stream_session_active.loadAcquire() != 0 &&
                render_pending_during_cycle.loadAcquire() > 1 &&
                quick_begin_wait_last_us > 1500 ? 1 : 0;
        }
    }

    {
        const qint64 render_setup_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (render_setup_after_us >= render_setup_begin_us) {
        const auto render_schedule_state = snapshotRenderScheduleState();
        logRenderPhaseStall(render_setup_after_us,
                            "render_setup",
                            render_setup_after_us - render_setup_begin_us,
                                queue_depth_before_render_setup,
                                depth_limit,
                                pending_frame_waiting,
                                render_active.loadAcquire() != 0,
                                render_schedule_state.first,
                                render_schedule_state.second);
        }
    }

    if (frame_mix.num_frames == 0) {
        if (replay_attempted && replay_enqueued) {
            close_started_frame(false);
            bool scheduled_pending = finalize_render();
            if (!scheduled_pending)
                schedulePostRenderWork();
            return;
        }

        const qint64 render_entry_us_local = last_render_entry_us.loadAcquire();
        const qint64 render_call_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        if (render_entry_us_local > 0 && render_call_begin_us >= render_entry_us_local)
            logLatencyStats("render_prep", render_call_begin_us - render_entry_us_local);
        if (render_call_begin_us >= render_setup_begin_us)
            logRenderSetupStats(render_call_begin_us, render_call_begin_us - render_setup_begin_us);
        const bool rendered = pl_render_image(placebo_renderer, nullptr, &target_frame, &params);
        if (!rendered) {
            qCWarning(chiakiGui) << "Failed to render held/overlay-only frame";
            close_started_frame(false);
            finalize_render();
            return;
        }
        const qint64 render_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        if (render_after_us >= render_call_begin_us) {
            logLatencyStats("render_image", render_after_us - render_call_begin_us);
            if (render_entry_us_local > 0 && render_after_us >= render_entry_us_local)
                logLatencyStats("render_work", render_after_us - render_entry_us_local);
        }

        close_started_frame(true);
        bool scheduled_pending = finalize_render();
        if (!scheduled_pending)
            schedulePostRenderWork();
        return;
    }

    if (hint_frame) {
        pl_rect2df crop = hint_frame->crop;
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
    if (hint_frame) {
        const float src_width = fabsf(pl_rect_w(hint_frame->crop));
        const float src_height = fabsf(pl_rect_h(hint_frame->crop));
        const float dst_width = fabsf(pl_rect_w(target_frame.crop));
        const float dst_height = fabsf(pl_rect_h(target_frame.crop));
        if (src_width > 0.0f && src_height > 0.0f) {
            const float upscale_factor = qMin(dst_width / src_width, dst_height / src_height);
            fsrcnnx_hook = select_spatial_hook(video_preset, configured_upscaler, upscale_factor,
                                               fsr_hook, fsrcnnx_hook_8, fsrcnnx_hook_16);
        }
    }
    const struct pl_hook *active_hooks[] = {fsrcnnx_hook};
    if (fsrcnnx_hook) {
        params.hooks = active_hooks;
        params.num_hooks = 1;
        switch (video_preset) {
        case VideoPreset::HighQualitySpatial:
            if (fsrcnnx_hook == fsr_hook)
                qCDebug(chiakiGui) << "Activating spatial upscaler: FSR (small upscale fallback)";
            else
                qCDebug(chiakiGui) << "Activating spatial upscaler: FSRCNNX x2 8-0-4-1";
            break;
        case VideoPreset::HighQualityAdvancedSpatial:
            if (fsrcnnx_hook == fsr_hook)
                qCDebug(chiakiGui) << "Activating spatial upscaler: FSR (small upscale fallback)";
            else
                qCDebug(chiakiGui) << "Activating spatial upscaler: FSRCNNX x2 16-0-4-1";
            break;
        case VideoPreset::Custom:
            if (fsrcnnx_hook == fsr_hook) {
                qCDebug(chiakiGui) << "Activating custom upscaler: FSR";
            } else {
                switch (configured_upscaler) {
                case PlaceboUpscaler::FSRCNNX8:
                qCDebug(chiakiGui) << "Activating custom upscaler: FSRCNNX x2 8-0-4-1";
                break;
                case PlaceboUpscaler::FSRCNNX16:
                qCDebug(chiakiGui) << "Activating custom upscaler: FSRCNNX x2 16-0-4-1";
                break;
                default:
                    break;
                }
            }
            break;
        default:
            break;
        }
    }

    for (int i = 0; i < frame_mix.num_frames; i++) {
        const struct pl_frame *mix_frame = frame_mix.frames[i];
        if (!mix_frame || mix_frame->num_planes <= 0) {
            qCWarning(chiakiGui) << "Frame mix contains an invalid frame, skipping render";
            if (placebo_reset_pending.loadAcquire() == 0)
                schedulePlaceboReset();
            close_started_frame(false);
            finalize_render();
            return;
        }
    }

    const qint64 render_setup_ready_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (render_setup_ready_us >= render_setup_config_after_us) {
        const auto render_schedule_state = snapshotRenderScheduleState();
        logRenderPhaseStall(render_setup_ready_us,
                            "render_setup_ready",
                            render_setup_ready_us - render_setup_config_after_us,
                            snapshotQueueDepth(),
                            depth_limit,
                            pending_frame_waiting,
                            render_active.loadAcquire() != 0,
                            render_schedule_state.first,
                            render_schedule_state.second);
    }

    const qint64 render_call_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (render_entry_us_local > 0 && render_call_begin_us >= render_entry_us_local)
        logLatencyStats("render_prep", render_call_begin_us - render_entry_us_local);
    if (render_call_begin_us >= render_setup_begin_us)
        logRenderSetupStats(render_call_begin_us, render_call_begin_us - render_setup_begin_us);
    if (!pl_render_image_mix(placebo_renderer, &frame_mix, &target_frame, &params))
    {
        qCWarning(chiakiGui) << "Failed to render Placebo frame!";
        close_started_frame(false);
        finalize_render();
        return;
    }
    {
        const qint64 render_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        if (render_after_us >= render_call_begin_us) {
            logLatencyStats("render_image", render_after_us - render_call_begin_us);
            if (render_entry_us_local > 0 && render_after_us >= render_entry_us_local)
                logLatencyStats("render_work", render_after_us - render_entry_us_local);
        }
    }

    close_started_frame(true);

    bool scheduled_pending = finalize_render();
    if (!scheduled_pending)
        schedulePostRenderWork();

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
