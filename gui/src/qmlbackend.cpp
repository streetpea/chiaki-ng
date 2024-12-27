#include "qmlbackend.h"
#include "qmlsettings.h"
#include "qmlmainwindow.h"
#include "streamsession.h"
#include "controllermanager.h"
#include "psnaccountid.h"
#include "psntoken.h"
#include "systemdinhibit.h"
#include "chiaki/remote/holepunch.h"
#if CHIAKI_GUI_ENABLE_STEAM_SHORTCUT
#include "steamtools.h"
#endif

#include <QUrlQuery>
#include <QGuiApplication>
#include <QPixmap>
#include <QImageReader>
#include <QProcessEnvironment>
#include <QDesktopServices>

#define PSN_DEVICES_TRIES 2
#define MAX_PSN_RECONNECT_TRIES 6
#define PSN_INTERNET_WAIT_SECONDS 5
#define WAKEUP_PSN_IGNORE_SECONDS 10
#define WAKEUP_WAIT_SECONDS 25
static QMutex chiaki_log_mutex;
static ChiakiLog *chiaki_log_ctx = nullptr;
static QtMessageHandler qt_msg_handler = nullptr;

static void msg_handler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QMutexLocker lock(&chiaki_log_mutex);
    if (!chiaki_log_ctx) {
        qt_msg_handler(type, context, msg);
        return;
    }
    ChiakiLogLevel chiaki_level;
    switch (type) {
    case QtDebugMsg:
        chiaki_level = CHIAKI_LOG_DEBUG;
        break;
    case QtInfoMsg:
        chiaki_level = CHIAKI_LOG_INFO;
        break;
    case QtWarningMsg:
        chiaki_level = CHIAKI_LOG_WARNING;
        break;
    case QtCriticalMsg:
        chiaki_level = CHIAKI_LOG_ERROR;
        break;
    case QtFatalMsg:
        chiaki_level = CHIAKI_LOG_ERROR;
        break;
    }
    chiaki_log(chiaki_log_ctx, chiaki_level, "%s", qPrintable(msg));
}

QmlRegist::QmlRegist(const ChiakiRegistInfo &regist_info, uint32_t log_mask, QObject *parent)
    : QObject(parent)
{
    chiaki_log_init(&chiaki_log, log_mask, &QmlRegist::log_cb, this);
    chiaki_regist_start(&chiaki_regist, &chiaki_log, &regist_info, &QmlRegist::regist_cb, this);
}

void QmlRegist::log_cb(ChiakiLogLevel level, const char *msg, void *user)
{
    chiaki_log_cb_print(level, msg, nullptr);
    auto r = static_cast<QmlRegist*>(user);
    QMetaObject::invokeMethod(r, std::bind(&QmlRegist::log, r, level, QString::fromUtf8(msg)), Qt::QueuedConnection);
}

void QmlRegist::regist_cb(ChiakiRegistEvent *event, void *user)
{
    auto r = static_cast<QmlRegist*>(user);
    switch (event->type) {
    case CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS:
        QMetaObject::invokeMethod(r, std::bind(&QmlRegist::success, r, *event->registered_host), Qt::QueuedConnection);
        QMetaObject::invokeMethod(r, &QObject::deleteLater, Qt::QueuedConnection);
        break;
    case CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED:
        QMetaObject::invokeMethod(r, &QmlRegist::failed, Qt::QueuedConnection);
        QMetaObject::invokeMethod(r, &QObject::deleteLater, Qt::QueuedConnection);
        break;
    default:
        break;
    }
}

QmlBackend::QmlBackend(Settings *settings, QmlMainWindow *window)
    : QObject(window)
    , settings(settings)
    , settings_qml(new QmlSettings(settings, this))
    , window(window)
{
    qt_msg_handler = qInstallMessageHandler(msg_handler);

    const char *uri = "org.streetpea.chiaking";
    qmlRegisterSingletonInstance(uri, 1, 0, "Chiaki", this);
    qmlRegisterUncreatableType<QmlMainWindow>(uri, 1, 0, "ChiakiWindow", {});
    qmlRegisterUncreatableType<QmlSettings>(uri, 1, 0, "ChiakiSettings", {});
    qmlRegisterUncreatableType<StreamSession>(uri, 1, 0, "ChiakiSession", {});

    QObject *frame_obj = new QObject();
    frame_thread = new QThread(frame_obj);
    frame_thread->setObjectName("frame");
    frame_thread->start();
    frame_obj->moveToThread(frame_thread);

    PsnConnectionWorker *worker = new PsnConnectionWorker;
    worker->moveToThread(&psn_connection_thread);
    connect(&psn_connection_thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &QmlBackend::psnConnect, worker, &PsnConnectionWorker::ConnectPsnConnection);
    connect(worker, &PsnConnectionWorker::resultReady, this, &QmlBackend::checkPsnConnection);
    psn_connection_thread.start();

    setConnectState(PsnConnectState::NotStarted);
    connect(settings, &Settings::RegisteredHostsUpdated, this, &QmlBackend::hostsChanged);
    connect(settings, &Settings::HiddenHostsUpdated, this, &QmlBackend::hiddenHostsChanged);
    connect(settings, &Settings::ManualHostsUpdated, this, &QmlBackend::hostsChanged);
    connect(settings, &Settings::CurrentProfileChanged, this, &QmlBackend::profileChanged);
    connect(&discovery_manager, &DiscoveryManager::HostsUpdated, this, &QmlBackend::updateDiscoveryHosts);
    discovery_manager.SetSettings(settings);
    setDiscoveryEnabled(true);
    connect(ControllerManager::GetInstance(), &ControllerManager::AvailableControllersUpdated, this, &QmlBackend::updateControllers);
    updateControllers();
    updateControllerMappings();
    connect(settings, &Settings::ControllerMappingsUpdated, this, &QmlBackend::updateControllerMappings);
    connect(this, &QmlBackend::controllersChanged, this, &QmlBackend::updateControllerMappings);
    auto_connect_mac = settings->GetAutoConnectHost().GetServerMAC();
    auto_connect_nickname = settings->GetAutoConnectHost().GetServerNickname();
    psn_auto_connect_timer = new QTimer(this);
    psn_auto_connect_timer->setSingleShot(true);
    psn_reconnect_tries = 0;
    psn_reconnect_timer = new QTimer(this);
    wakeup_start_timer = new QTimer(this);
    wakeup_start_timer->setSingleShot(true);
    if(autoConnect() && !auto_connect_nickname.isEmpty())
    {
        connect(psn_auto_connect_timer, &QTimer::timeout, this, [this]
        {
            int i = 0;
            for (const auto &host : std::as_const(psn_hosts))
            {
                if(host.GetName() == auto_connect_nickname)
                {
                    int index = discovery_manager.GetHosts().size() + this->settings->GetManualHosts().size() + i;
                    connectToHost(index);
                    return;
                }
                i++;
            }
            qCWarning(chiakiGui) << "Couldn't find PSN host with the requested nickname: " << auto_connect_nickname;
        });
        psn_auto_connect_timer->start(PSN_INTERNET_WAIT_SECONDS * 1000);
    }
    connect(psn_reconnect_timer, &QTimer::timeout, this, [this]{
        QString refresh = this->settings->GetPsnRefreshToken();
        if(refresh.isEmpty())
        {
            qCWarning(chiakiGui) << "No refresh token found, can't refresh PSN token to use PSN remote connection";
            psn_reconnect_tries = 0;
            resume_session = false;
            psn_reconnect_timer->stop();
            setConnectState(PsnConnectState::ConnectFailed);
            return;
        }
        PSNToken *psnToken = new PSNToken(this->settings, this);
        connect(psnToken, &PSNToken::PSNTokenError, this, [this](const QString &error) {
            qCWarning(chiakiGui) << "Internet is currently down...waiting 5 seconds" << error;
            psn_reconnect_tries++;
            if(psn_reconnect_tries < MAX_PSN_RECONNECT_TRIES)
                return;
            else
            {
                resume_session = false;
                psn_reconnect_tries = 0;
                psn_reconnect_timer->stop();
                setConnectState(PsnConnectState::ConnectFailed);
            }
        });
        connect(psnToken, &PSNToken::UnauthorizedError, this, &QmlBackend::psnCredsExpired);
        connect(psnToken, &PSNToken::PSNTokenSuccess, this, []() {
            qCWarning(chiakiGui) << "PSN Remote Connection Tokens Refreshed. Internet is back up";
        });
        connect(psnToken, &PSNToken::PSNTokenSuccess, this, [this]() {
            resume_session = false;
            psn_reconnect_tries = 0;
            psn_reconnect_timer->stop();
            createSession(session_info);
        });
        QString refresh_token = this->settings->GetPsnRefreshToken();
        psnToken->RefreshPsnToken(std::move(refresh_token));
    });
    connect(wakeup_start_timer, &QTimer::timeout, this, [this]
    {
        emit wakeupStartFailed();
    });
    psn_auto_connect_timer->start(PSN_INTERNET_WAIT_SECONDS * 1000);
    sleep_inhibit = new SystemdInhibit(QGuiApplication::applicationName(), tr("Remote Play session"), "sleep", "delay", this);
    connect(sleep_inhibit, &SystemdInhibit::sleep, this, [this]() {
        qCInfo(chiakiGui) << "About to sleep";
        if (session) {
            if (this->settings->GetSuspendAction() == SuspendAction::Sleep)
                session->GoToBed();
            session->Stop();
            if(!session_info.duid.isEmpty())
                psnCancel(true);
            resume_session = true;
        }
    });
    connect(sleep_inhibit, &SystemdInhibit::resume, this, [this]() {
        qCInfo(chiakiGui) << "Resumed from sleep";
        if (resume_session) {
            qCInfo(chiakiGui) << "Resuming session...";
            resume_session = false;
            if(session_info.duid.isEmpty())
            {
                createSession({
                    session_info.settings,
                    session_info.target,
                    session_info.host,
                    session_info.nickname,
                    session_info.regist_key,
                    session_info.morning,
                    session_info.initial_login_pin,
                    session_info.duid,
                    session_info.fullscreen,
                    session_info.zoom,
                    session_info.stretch,
                });
            }
            else
            {
                emit showPsnView();
                setConnectState(PsnConnectState::WaitingForInternet);
                psn_reconnect_timer->start(PSN_INTERNET_WAIT_SECONDS * 1000);
            }
        }
    });
    refreshPsnToken();
}

QmlBackend::~QmlBackend()
{
    if(session)
    {
        chiaki_log_mutex.lock();
        chiaki_log_ctx = nullptr;
        chiaki_log_mutex.unlock();
        delete session;
        session = nullptr;
    }
    frame_thread->quit();
    frame_thread->wait();
    delete frame_thread->parent();
    delete psn_auto_connect_timer;
    delete psn_reconnect_timer;
    psn_connection_thread.quit();
    psn_connection_thread.wait();
    delete psn_connection_thread.parent();
}

QmlMainWindow *QmlBackend::qmlWindow() const
{
    return window;
}

QmlSettings *QmlBackend::qmlSettings() const
{
    return settings_qml;
}

StreamSession *QmlBackend::qmlSession() const
{
    return session;
}

QList<QmlController*> QmlBackend::qmlControllers() const
{
    return controllers.values();
}

void QmlBackend::profileChanged()
{
    QString profile = settings->GetCurrentProfile();
    Settings *settings_copy = new Settings(profile);
    if(settings_allocd)
        delete settings;
    settings_allocd = true;
    settings = settings_copy;
    emit hostsChanged();
    updateControllerMappings();
    connect(settings, &Settings::RegisteredHostsUpdated, this, &QmlBackend::hostsChanged);
    connect(settings, &Settings::HiddenHostsUpdated, this, &QmlBackend::hiddenHostsChanged);
    connect(settings, &Settings::ManualHostsUpdated, this, &QmlBackend::hostsChanged);
    connect(settings, &Settings::CurrentProfileChanged, this, &QmlBackend::profileChanged);
    connect(settings, &Settings::ControllerMappingsUpdated, this, &QmlBackend::updateControllerMappings);
    settings_qml->setSettings(settings);
    discovery_manager.SetSettings(settings);
    window->setSettings(settings);
    setDiscoveryEnabled(true);

    auto_connect_mac = settings->GetAutoConnectHost().GetServerMAC();
    auto_connect_nickname = settings->GetAutoConnectHost().GetServerNickname();
    delete psn_reconnect_timer;
    delete psn_auto_connect_timer;
    psn_auto_connect_timer = new QTimer(this);
    psn_auto_connect_timer->setSingleShot(true);
    psn_reconnect_tries = 0;
    psn_reconnect_timer = new QTimer(this);
    if(autoConnect() && !auto_connect_nickname.isEmpty())
    {
        connect(psn_auto_connect_timer, &QTimer::timeout, this, [this]
        {
            int i = 0;
            for (const auto &host : std::as_const(psn_hosts))
            {
                if(host.GetName() == auto_connect_nickname)
                {
                    int index = discovery_manager.GetHosts().size() + this->settings->GetManualHosts().size() + i;
                    connectToHost(index);
                    return;
                }
                i++;
            }
            qCWarning(chiakiGui) << "Couldn't find PSN host with the requested nickname: " << auto_connect_nickname;
        });
        psn_auto_connect_timer->start(PSN_INTERNET_WAIT_SECONDS * 1000);
    }
    connect(psn_reconnect_timer, &QTimer::timeout, this, [this]{
        QString refresh = this->settings->GetPsnRefreshToken();
        if(refresh.isEmpty())
        {
            qCWarning(chiakiGui) << "No refresh token found, can't refresh PSN token to use PSN remote connection";
            psn_reconnect_tries = 0;
            resume_session = false;
            psn_reconnect_timer->stop();
            setConnectState(PsnConnectState::ConnectFailed);
            return;
        }
        PSNToken *psnToken = new PSNToken(this->settings, this);
        connect(psnToken, &PSNToken::PSNTokenError, this, [this](const QString &error) {
            qCWarning(chiakiGui) << "Internet is currently down...waiting 5 seconds" << error;
            psn_reconnect_tries++;
            if(psn_reconnect_tries < MAX_PSN_RECONNECT_TRIES)
                return;
            else
            {
                resume_session = false;
                psn_reconnect_tries = 0;
                psn_reconnect_timer->stop();
                setConnectState(PsnConnectState::ConnectFailed);
            }
        });
        connect(psnToken, &PSNToken::UnauthorizedError, this, &QmlBackend::psnCredsExpired);
        connect(psnToken, &PSNToken::PSNTokenSuccess, this, []() {
            qCWarning(chiakiGui) << "PSN Remote Connection Tokens Refreshed. Internet is back up";
        });
        connect(psnToken, &PSNToken::PSNTokenSuccess, this, [this]() {
            resume_session = false;
            psn_reconnect_tries = 0;
            psn_reconnect_timer->stop();
            createSession(session_info);
        });
        QString refresh_token = this->settings->GetPsnRefreshToken();
        psnToken->RefreshPsnToken(std::move(refresh_token));
    });
    delete sleep_inhibit;
    sleep_inhibit = new SystemdInhibit(QGuiApplication::applicationName(), tr("Remote Play session"), "sleep", "delay", this);
    connect(sleep_inhibit, &SystemdInhibit::sleep, this, [this]() {
        qCInfo(chiakiGui) << "About to sleep";
        if (session) {
            if (this->settings->GetSuspendAction() == SuspendAction::Sleep)
                session->GoToBed();
            session->Stop();
            if(!session_info.duid.isEmpty())
                psnCancel(true);
            resume_session = true;
        }
    });
    connect(sleep_inhibit, &SystemdInhibit::resume, this, [this]() {
        qCInfo(chiakiGui) << "Resumed from sleep";
        if (resume_session) {
            qCInfo(chiakiGui) << "Resuming session...";
            resume_session = false;
            if(session_info.duid.isEmpty())
            {
                createSession({
                    session_info.settings,
                    session_info.target,
                    session_info.host,
                    session_info.nickname,
                    session_info.regist_key,
                    session_info.morning,
                    session_info.initial_login_pin,
                    session_info.duid,
                    session_info.fullscreen,
                    session_info.zoom,
                    session_info.stretch,
                });
            }
            else
            {
                emit showPsnView();
                setConnectState(PsnConnectState::WaitingForInternet);
                psn_reconnect_timer->start(PSN_INTERNET_WAIT_SECONDS * 1000);
            }
        }
    });
    refreshPsnToken();
    emit hostsChanged();
    emit hiddenHostsChanged();
}

bool QmlBackend::discoveryEnabled() const
{
    return discovery_manager.GetActive();
}

void QmlBackend::setDiscoveryEnabled(bool enabled)
{
    discovery_manager.SetActive(enabled);
    emit discoveryEnabledChanged();
}

QmlBackend::PsnConnectState QmlBackend::connectState() const
{
    return psn_connect_state;
}

void QmlBackend::checkNickname(QString nickname)
{
    if(!session)
        return;
    if(!settings->GetNicknameRegisteredHostRegistered(nickname))
    {
        emit error(tr("PS4 Console Unregistered"), tr("Can't proceed...please register your PS4 console locally"));
        session->Stop();
    }
}

void QmlBackend::setConnectState(PsnConnectState connect_state)
{
    psn_connect_state = connect_state;
    emit connectStateChanged();
}

QVariantList QmlBackend::hosts() const
{
    QVariantList out;
    QList<QString> discovered_nicknames;
    QList<ManualHost> discovered_manual_hosts;
    size_t registered_discovered_ps4s = 0;
    auto manual_hosts = settings->GetManualHosts();
    for (const auto &host : discovery_manager.GetHosts()) {
        QVariantMap m;
        HostMAC host_mac = host.GetHostMAC();
        bool registered = settings->GetRegisteredHostRegistered(host_mac);
        bool hidden = settings->GetHiddenHostHidden(host_mac);
        if(registered && hidden)
        {
            settings->RemoveHiddenHost(host_mac);
            bool hidden = false;
        }
        // Update hidden host nickname if it's changed
        if(hidden)
        {
            auto hidden_host = settings->GetHiddenHost(host_mac);
            if(hidden_host.GetNickname() != host.host_name)
            {
                hidden_host.SetNickname(host.host_name);
                settings->RemoveHiddenHost(host_mac);
                settings->AddHiddenHost(hidden_host);
            }
        }
        m["discovered"] = true;
        bool manual = false;
        for(int i = 0; i < manual_hosts.length(); i++)
        {
            const auto &manual_host = manual_hosts.at(i);
            if(manual_host.GetRegistered() && manual_host.GetMAC() == host_mac && manual_host.GetHost() == host.host_addr)
            {
                manual = true;
                discovered_manual_hosts.append(manual_host);
            }
        }
        m["manual"] = manual;
        m["name"] = host.host_name;
        m["duid"] = "";
        m["address"] = host.host_addr;
        m["ps5"] = host.ps5;
        m["mac"] = host_mac.ToString();
        m["state"] = chiaki_discovery_host_state_string(host.state);
        m["app"] = host.running_app_name;
        m["titleId"] = host.running_app_titleid;
        m["registered"] = registered;
        m["display"] = hidden ? false : true;
        discovered_nicknames.append(host.host_name);
        out.append(m);
        if(!host.ps5 && registered)
            registered_discovered_ps4s++;
    }
    for (const auto &host : settings->GetManualHosts()) {
        QVariantMap m;
        m["discovered"] = false;
        m["manual"] = true;
        m["name"] = host.GetHost();
        m["duid"] = "";
        m["address"] = host.GetHost();
        m["state"] = "unknown";
        m["registered"] = false;
        m["display"] = discovered_manual_hosts.contains(host) ? false : true;
        if (host.GetRegistered() && settings->GetRegisteredHostRegistered(host.GetMAC())) {
            auto registered = settings->GetRegisteredHost(host.GetMAC());
            m["registered"] = true;
            m["name"] = registered.GetServerNickname();
            m["ps5"] = chiaki_target_is_ps5(registered.GetTarget());
            m["mac"] = registered.GetServerMAC().ToString();
        }
        out.append(m);
    }
    if(registered_discovered_ps4s >= settings->GetPS4RegisteredHostsRegistered())
        discovered_nicknames.append(QString("Main PS4 Console"));
    for (const auto &host : psn_hosts) {
        QVariantMap m;
        // Only list PSN remote hosts that aren't discovered locally
        bool discovered = false;
        for (int i = 0; i < discovered_nicknames.size(); ++i)
        {
            if (discovered_nicknames.at(i) == host.GetName())
                discovered = true;
        }
        for (int i = 0; i < waking_sleeping_nicknames.size(); ++i)
        {
            if (waking_sleeping_nicknames.at(i) == host.GetName())
                discovered = true;
        }
        if(discovered)
            continue;
        m["discovered"] = false;
        m["manual"] = false;
        m["display"] = true;
        m["name"] = host.GetName();
        m["duid"] = host.GetDuid();
        m["address"] = "";
        m["registered"] = true;
        m["ps5"] = host.IsPS5();
        out.append(m);
    }
    return out;
}

bool QmlBackend::autoConnect() const
{
    return auto_connect_mac.GetValue();
}

void QmlBackend::psnCancel(bool stop_thread)
{
    session->CancelPsnConnection(stop_thread);
}

void QmlBackend::checkPsnConnection(const ChiakiErrorCode &err)
{
    switch(err)
    {
        case CHIAKI_ERR_SUCCESS:
            setConnectState(PsnConnectState::LinkingConsole);
            psnSessionStart();
            break;
        case CHIAKI_ERR_HOST_DOWN:
            setConnectState(PsnConnectState::ConnectFailedStart);
            if(session)
            {
                chiaki_log_mutex.lock();
                chiaki_log_ctx = nullptr;
                chiaki_log_mutex.unlock();
                delete session;
                session = nullptr;
                setDiscoveryEnabled(true);
            }
            break;
        case CHIAKI_ERR_HOST_UNREACH:
            setConnectState(PsnConnectState::ConnectFailedConsoleUnreachable);
            if(session)
            {
                chiaki_log_mutex.lock();
                chiaki_log_ctx = nullptr;
                chiaki_log_mutex.unlock();
                delete session;
                session = nullptr;
                setDiscoveryEnabled(true);
            }
            break;
        default:
            setConnectState(PsnConnectState::ConnectFailed);
            if(session)
            {
                chiaki_log_mutex.lock();
                chiaki_log_ctx = nullptr;
                chiaki_log_mutex.unlock();
                delete session;
                session = nullptr;
                setDiscoveryEnabled(true);
            }
            break;
    }
}

void QmlBackend::psnSessionStart()
{
    try {
        session->Start();
    } catch (const Exception &e) {
        chiaki_log_mutex.lock();
        chiaki_log_ctx = nullptr;
        chiaki_log_mutex.unlock();
        delete session;
        session = nullptr;
        emit error(tr("Stream failed"), tr("Failed to start Stream Session: %1").arg(e.what()));
        return;
    }

    sleep_inhibit->inhibit();
}

void QmlBackend::createSession(const StreamSessionConnectInfo &connect_info)
{
    if (autoConnect()) {
        auto_connect_mac = {};
        emit autoConnectChanged();
    }

    if (session) {
        qCWarning(chiakiGui) << "Another session is already active";
        return;
    }

    session_info = connect_info;
    QStringList availableDecoders = settings_qml->availableDecoders();
    if(session_info.hw_decoder == "auto")
    {
        session_info.hw_decoder = QString();
#if defined(Q_OS_LINUX)
        if(availableDecoders.contains("vulkan"))
        {
            qCInfo(chiakiGui) << "Auto hw decoder selecting vulkan";
            session_info.hw_decoder = "vulkan";
        }
        else if(availableDecoders.contains("vaapi"))
        {
            qCInfo(chiakiGui) << "Auto hw decoder selecting vaapi";
            session_info.hw_decoder = "vaapi";
        }
#elif defined(Q_OS_WIN)
        if(availableDecoders.contains("vulkan"))
        {
            qCInfo(chiakiGui) << "Auto hw decoder selecting vulkan";
            session_info.hw_decoder = "vulkan";
        }
        else if(availableDecoders.contains("d3d11va"))
        {
            qCInfo(chiakiGui) << "Auto hw decoder selecting d3d11va";
            session_info.hw_decoder = "d3d11va";
        }
#elif defined(Q_OS_MACOS)
        if(availableDecoders.contains("videotoolbox"))
        {
            qCInfo(chiakiGui) << "Auto hw decoder selecting videotoolbox";
            session_info.hw_decoder = "videotoolbox";
        }
#endif
    }
    if (session_info.hw_decoder == "vulkan") {
        session_info.hw_device_ctx = window->vulkanHwDeviceCtx();
        if (!session_info.hw_device_ctx)
        {
            session_info.hw_decoder.clear();
            qCInfo(chiakiGui) << "vulkan video decoding not supported by your gpu driver, retrying other hw video decoders";
#if defined(Q_OS_LINUX)
            if(availableDecoders.contains("vaapi"))
            {
                qCInfo(chiakiGui) << "Falling back to vaapi";
                session_info.hw_decoder = "vaapi";
            }
#elif defined(Q_OS_WIN)
            if(availableDecoders.contains("d3d11va"))
            {
                qCInfo(chiakiGui) << "Falling back to d3d11va";
                session_info.hw_decoder = "d3d11va";
            }
#endif
        }
        if(session_info.hw_decoder.isEmpty())
            qCInfo(chiakiGui) << "Falling back to software decoder";
    }

    try {
        session = new StreamSession(session_info, this);
    } catch (const Exception &e) {
        emit error(tr("Stream failed"), tr("Failed to initialize Stream Session: %1").arg(e.what()));
        return;
    }

    connect(session, &StreamSession::FfmpegFrameAvailable, frame_thread->parent(), [this]() {
        ChiakiFfmpegDecoder *decoder = session->GetFfmpegDecoder();
        if (!decoder) {
            qCCritical(chiakiGui) << "Session has no FFmpeg decoder";
            return;
        }
        int32_t frames_lost;
        AVFrame *frame = chiaki_ffmpeg_decoder_pull_frame(decoder, &frames_lost);
        if (!frame)
            return;

        static const QSet<int> zero_copy_formats = {
            AV_PIX_FMT_VULKAN,
#ifdef Q_OS_LINUX
            AV_PIX_FMT_VAAPI,
#endif
        };
        if (frame->hw_frames_ctx && (!zero_copy_formats.contains(frame->format) || disable_zero_copy)) {
            AVFrame *sw_frame = av_frame_alloc();
            if (av_hwframe_transfer_data(sw_frame, frame, 0) < 0) {
                qCWarning(chiakiGui) << "Failed to transfer frame from hardware";
                av_frame_unref(frame);
                av_frame_free(&sw_frame);
                return;
            }
            av_frame_copy_props(sw_frame, frame);
            av_frame_unref(frame);
            frame = sw_frame;
        }
        QMetaObject::invokeMethod(window, std::bind(&QmlMainWindow::presentFrame, window, frame, frames_lost));
    });

    connect(session, &StreamSession::SessionQuit, this, [this](ChiakiQuitReason reason, const QString &reason_str) {
        if (chiaki_quit_reason_is_error(reason)) {
            QString m = tr("Chiaki Session has quit") + ":\n" + chiaki_quit_reason_string(reason);
            if (!reason_str.isEmpty())
                m += "\n" + tr("Reason") + ": \"" + reason_str + "\"";
            emit sessionError(tr("Session has quit"), m);
        }

        chiaki_log_mutex.lock();
        chiaki_log_ctx = nullptr;
        chiaki_log_mutex.unlock();

        session->deleteLater();
        session = nullptr;
        emit sessionChanged(session);

        sleep_inhibit->release();
        setDiscoveryEnabled(true);
    });

    connect(session, &StreamSession::LoginPINRequested, this, [this, connect_info](bool incorrect) {
        if (!connect_info.initial_login_pin.isEmpty() && incorrect == false)
            session->SetLoginPIN(connect_info.initial_login_pin);
        else
            emit sessionPinDialogRequested();
    });

    connect(session, &StreamSession::DataHolepunchProgress, this, [this](bool finished) {
        if(finished)
        {
            setConnectState(PsnConnectState::DataConnectionFinished);
            emit sessionChanged(session);
        }
        else
            setConnectState(PsnConnectState::DataConnectionStart);
    });

    connect(session, &StreamSession::NicknameReceived, this, &QmlBackend::checkNickname);

    connect(session, &StreamSession::ConnectedChanged, this, [this]() {
        if (session->IsConnected())
            setDiscoveryEnabled(false);
    });

    if (window->windowState() != Qt::WindowFullScreen)
    {
        if(settings->GetWindowType() == WindowType::CustomResolution)
        {
            window->resize(settings->GetCustomResolutionWidth(), settings->GetCustomResolutionHeight());
            window->setMaximumSize(QSize(settings->GetCustomResolutionWidth(), settings->GetCustomResolutionHeight()));
        }
        else
            window->resize(connect_info.video_profile.width, connect_info.video_profile.height);
    }

    chiaki_log_mutex.lock();
    chiaki_log_ctx = session->GetChiakiLog();
    chiaki_log_mutex.unlock();

    if(connect_info.duid.isEmpty())
    {
        if(!wakeup_nickname.isEmpty())
        {
            wakeup_start = true;
            wakeup_start_timer->start(WAKEUP_WAIT_SECONDS * 1000);
            emit wakeupStartInitiated();
        }
        else
        {
            try {
                session->Start();
            } catch (const Exception &e) {
                emit error(tr("Stream failed"), tr("Failed to start Stream Session: %1").arg(e.what()));
                chiaki_log_mutex.lock();
                chiaki_log_ctx = nullptr;
                chiaki_log_mutex.unlock();
                delete session;
                session = nullptr;
                return;
            }
            emit sessionChanged(session);

            sleep_inhibit->inhibit();
        }
    }
    else
    {
        setDiscoveryEnabled(false);
        emit showPsnView();
        setConnectState(PsnConnectState::InitiatingConnection);
        emit psnConnect(session, session_info.duid, chiaki_target_is_ps5(session_info.target));
    }
}

bool QmlBackend::closeRequested()
{
    if (!session)
        return true;

    bool stop = true;
    if (session->IsConnected()) {
        switch (settings->GetDisconnectAction()) {
        case DisconnectAction::Ask:
            stop = false;
            emit sessionStopDialogRequested();
            break;
        case DisconnectAction::AlwaysSleep:
            session->GoToBed();
            break;
        default:
            break;
        }
    }

    if (stop)
        session->Stop();

    return false;
}

void QmlBackend::deleteHost(int index)
{
    auto server = displayServerAt(index);
    auto id = server.manual_host.GetID();
    if (!server.valid || (id < 0))
        return;
    settings->RemoveManualHost(id);
}

void QmlBackend::wakeUpHost(int index, QString nickname)
{
    auto server = displayServerAt(index);
    if (!server.valid)
        return;
    if (!nickname.isEmpty())
    {
        waking_sleeping_nicknames.append(nickname);
        QTimer::singleShot(WAKEUP_PSN_IGNORE_SECONDS * 1000, [this, nickname]{
            waking_sleeping_nicknames.removeOne(nickname);
            emit hostsChanged();
        });
    }
    sendWakeup(server);
}

void QmlBackend::setConsolePin(int index, QString console_pin)
{
    auto server = displayServerAt(index);
    if (!server.valid)
        return;
    server.registered_host.SetConsolePin(server.registered_host, std::move(console_pin));
    settings->AddRegisteredHost(server.registered_host);
}

void QmlBackend::addManualHost(int index, const QString &address)
{
    HostMAC hmac;
    QList<RegisteredHost> registered_hosts = settings->GetRegisteredHosts();
    bool registered = (index >= 0 && (index < registered_hosts.length()));
    if (registered)
        hmac = registered_hosts.at(index).GetServerMAC();
    ManualHost host(-1, address, registered, hmac);
    settings->SetManualHost(host);
}

void QmlBackend::hideHost(const QString &mac_string, const QString &host_nickname)
{
    QByteArray mac_array = QByteArray::fromHex(mac_string.toUtf8());
    if (mac_array.size() != 6)
    {
        qCCritical(chiakiGui) << "Invalid host mac:" << mac_string.toUtf8();
        qCCritical(chiakiGui) << "Aborting hidden host creation because mac string couldn't be converted to a valid host mac!";
        qCCritical(chiakiGui) << "Received an array of unexpected size. Expected: 6 bytes, Received:" << mac_array.size() << "bytes";
        return;
    }
    HostMAC mac((const uint8_t *)mac_array.constData());
    HiddenHost hidden_host(mac, host_nickname);
    settings->AddHiddenHost(hidden_host);
    emit hostsChanged();
}

void QmlBackend::unhideHost(const QString &mac_string)
{
    QByteArray mac_array = QByteArray::fromHex(mac_string.toUtf8());
    const char *mac_ptr = mac_array.constData();
    if (strlen(mac_ptr) != 6)
    {
        qCCritical(chiakiGui) << " Aborting hidden host creation because mac string couldn't be converted to a valid host mac!";
        return;
    }
    HostMAC mac((const uint8_t *)mac_ptr);
    settings->RemoveHiddenHost(mac);
    emit hostsChanged();
}

QVariantList QmlBackend::hiddenHosts() const
{
    QVariantList out;
    for (const auto &host : settings->GetHiddenHosts()) {
        QVariantMap m;
        m["name"] = host.GetNickname();
        m["mac"] = host.GetMAC().ToString();
        out.append(m);
    }
    return out;
}


bool QmlBackend::registerHost(const QString &host, const QString &psn_id, const QString &pin, const QString &cpin, bool broadcast, int target, const QJSValue &callback)
{
    ChiakiRegistInfo info = {};
    QByteArray hostb = host.toUtf8();
    info.host = hostb.constData();
    info.target = static_cast<ChiakiTarget>(target);
    info.broadcast = broadcast;
    info.pin = (uint32_t)pin.toULong();
    info.console_pin = (uint32_t)cpin.toULong();
    info.holepunch_info = nullptr;
    info.rudp = nullptr;
    QByteArray psn_idb;
    if (target == CHIAKI_TARGET_PS4_8) {
        psn_idb = psn_id.toUtf8();
        info.psn_online_id = psn_idb.constData();
    } else {
        QByteArray account_id = QByteArray::fromBase64(psn_id.toUtf8());
        if (account_id.size() != CHIAKI_PSN_ACCOUNT_ID_SIZE) {
            emit error(tr("Invalid Account-ID"), tr("The PSN Account-ID must be exactly %1 bytes encoded as base64.").arg(CHIAKI_PSN_ACCOUNT_ID_SIZE));
            return false;
        }
        info.psn_online_id = nullptr;
        memcpy(info.psn_account_id, account_id.constData(), CHIAKI_PSN_ACCOUNT_ID_SIZE);
    }
    auto regist = new QmlRegist(info, settings->GetLogLevelMask(), this);
    connect(regist, &QmlRegist::log, this, [callback](ChiakiLogLevel level, QString msg) {
        QJSValue cb = callback;
        if (cb.isCallable())
            cb.call({QString("[%1] %2").arg(chiaki_log_level_char(level)).arg(msg), true, false});
    });
    connect(regist, &QmlRegist::failed, this, [this, callback]() {
        QJSValue cb = callback;
        if (cb.isCallable())
            cb.call({QString(), false, true});

        regist_dialog_server = {};
    });
    connect(regist, &QmlRegist::success, this, [this, host, callback](const RegisteredHost &rhost) {
        QJSValue cb = callback;
        if (cb.isCallable())
            cb.call({QString(), true, true});

        settings->AddRegisteredHost(rhost);
        if(regist_dialog_server.discovered == false)
        {
            ManualHost manual_host = regist_dialog_server.manual_host;
            if(manual_host.GetHost().isEmpty())
                manual_host.SetHost(host);
            manual_host.Register(rhost);
            settings->SetManualHost(manual_host);
        }
    });
    return true;
}

void QmlBackend::connectToHost(int index, QString nickname)
{
    auto server = displayServerAt(index);
    if (!server.valid)
        return;

    if (!server.registered) {
        regist_dialog_server = server;
        emit registDialogRequested(server.GetHostAddr(), server.IsPS5());
        return;
    }

    if (server.discovered && server.discovery_host.state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY)
    {
        if(!sendWakeup(server))
        {
            qCWarning(chiakiGui) << "Couldn't wakeup server";
            return;
        }
        if(nickname.isEmpty())
        {
            qCWarning(chiakiGui) << "No nickname given for registered connection, not connecting...";
            return;
        }
        waking_sleeping_nicknames.append(nickname);
        QTimer::singleShot(WAKEUP_PSN_IGNORE_SECONDS * 1000, [this, nickname]{
            waking_sleeping_nicknames.removeOne(nickname);
            emit hostsChanged();
        });
        wakeup_nickname = nickname;

    }

    bool fullscreen = false, zoom = false, stretch = false;
    switch (settings->GetWindowType()) {
    case WindowType::SelectedResolution:
        break;
    case WindowType::CustomResolution:
        break;
    case WindowType::Fullscreen:
        fullscreen = true;
        break;
    case WindowType::Zoom:
        zoom = true;
        break;
    case WindowType::Stretch:
        stretch = true;
        break;
    default:
        break;
    }
    emit windowTypeUpdated(settings->GetWindowType());

    resume_session = false;
    if(server.duid.isEmpty())
    {
        QString host = server.GetHostAddr();
        StreamSessionConnectInfo info(
                settings,
                server.registered_host.GetTarget(),
                std::move(host),
                std::move(nickname),
                server.registered_host.GetRPRegistKey(),
                server.registered_host.GetRPKey(),
                server.registered_host.GetConsolePin(),
                server.duid,
                fullscreen,
                zoom,
                stretch);
        createSession(info);
    }
    else
    {
        StreamSessionConnectInfo info(
                settings,
                server.psn_host.GetTarget(),
                QString(),
                QString(),
                QByteArray(),
                QByteArray(),
                server.registered_host.GetConsolePin(),
                server.duid,
                fullscreen,
                zoom,
                stretch);

        QString expiry_s = settings->GetPsnAuthTokenExpiry();
        QString refresh = settings->GetPsnRefreshToken();
        if(expiry_s.isEmpty() || refresh.isEmpty())
            return;
        QDateTime expiry = QDateTime::fromString(expiry_s, settings->GetTimeFormat());
        // give 1 minute buffer
        QDateTime now = QDateTime::currentDateTime().addSecs(60);
        if(now > expiry)
        {
            PSNToken *psnToken = new PSNToken(settings, this);
            connect(psnToken, &PSNToken::PSNTokenError, this, [this](const QString &error) {
                qCWarning(chiakiGui) << "Could not refresh token. Automatic PSN Connection Unavailable!" << error;
            });
            connect(psnToken, &PSNToken::UnauthorizedError, this, &QmlBackend::psnCredsExpired);
            connect(psnToken, &PSNToken::PSNTokenSuccess, this, []() {
                qCWarning(chiakiGui) << "PSN Remote Connection Tokens Refreshed.";
            });
            connect(psnToken, &PSNToken::PSNTokenSuccess, this, [this, info]() {
                createSession(info);
            });
            QString refresh_token = settings->GetPsnRefreshToken();
            psnToken->RefreshPsnToken(std::move(refresh_token));
        }
        else
            createSession(info);
    }
}

void QmlBackend::stopSession(bool sleep)
{
    if (!session)
        return;

    if (!session_info.nickname.isEmpty())
    {
        waking_sleeping_nicknames.append(session_info.nickname);
        QTimer::singleShot(WAKEUP_PSN_IGNORE_SECONDS * 1000, [this]{
            waking_sleeping_nicknames.removeOne(session_info.nickname);
            emit hostsChanged();
        });
    }

    if (sleep)
        session->GoToBed();

    session->Stop();
}

void QmlBackend::sessionGoHome()
{
    if (!session)
        return;

    session->GoHome();
}

void QmlBackend::enterPin(const QString &pin)
{
    qCInfo(chiakiGui) << "Set login pin " << pin;
    if (session)
        session->SetLoginPIN(pin);
}

QUrl QmlBackend::psnLoginUrl() const
{
    size_t duid_size = CHIAKI_DUID_STR_SIZE;
    char duid[duid_size];
    chiaki_holepunch_generate_client_device_uid(duid, &duid_size);
    return QUrl(PSNAuth::LOGIN_URL + "duid=" + QString(duid) + "&");
}

bool QmlBackend::handlePsnLoginRedirect(const QUrl &url)
{
    if (!url.toString().startsWith(QString::fromStdString(PSNAuth::REDIRECT_PAGE)))
    {
        emit psnLoginAccountIdError(QString("Redirect URL invalid does not start with:\n") + QString::fromStdString(PSNAuth::REDIRECT_PAGE));
        return false;
    }

    const QString code = QUrlQuery(url).queryItemValue("code");
    if (code.isEmpty()) {
        qCWarning(chiakiGui) << "Invalid code from redirect url";
        emit psnLoginAccountIdError("Redirect URL invalid");
        return false;
    }
    PSNAccountID *psnId = new PSNAccountID(settings, this);
    connect(psnId, &PSNAccountID::AccountIDResponse, this, [this, psnId](const QString &accountId) {
        psnId->deleteLater();
        emit psnLoginAccountIdDone(accountId);
    });
    connect(psnId, &PSNAccountID::AccountIDResponse, this, &QmlBackend::updatePsnHosts);
    connect(psnId, &PSNAccountID::AccountIDError, [this](const QString &error) {
        qCWarning(chiakiGui) << "Could not retrieve psn token or account Id!" << error;
        emit psnLoginAccountIdError(error);
    });
    psnId->GetPsnAccountId(code);
    emit psnTokenChanged();
    return true;
}

void QmlBackend::stopAutoConnect()
{
    auto_connect_mac = {};
    if(!wakeup_nickname.isEmpty())
    {
        wakeup_start_timer->stop();
        wakeup_nickname.clear();
        if(wakeup_start)
        {
            wakeup_start = false;
            chiaki_log_mutex.lock();
            chiaki_log_ctx = nullptr;
            chiaki_log_mutex.unlock();

            session->deleteLater();
            session = nullptr;
        }
    }
    emit autoConnectChanged();
}

QmlBackend::DisplayServer QmlBackend::displayServerAt(int index) const
{
    if (index < 0)
        return {};
    auto discovered = discovery_manager.GetHosts();
    auto manual = settings->GetManualHosts();
    if (index < discovered.size()) {
        DisplayServer server;
        server.valid = true;
        server.discovered = true;
        server.discovery_host = discovered.at(index);
        auto host_mac = server.discovery_host.GetHostMAC();
        server.registered = settings->GetRegisteredHostRegistered(host_mac);
        server.duid = QString();
        if (server.registered)
            server.registered_host = settings->GetRegisteredHost(host_mac);
        for (int i = 0; i < manual.size(); i++)
        {
            const auto &manual_host = manual.at(i);
            if(manual_host.GetRegistered() && manual_host.GetMAC() == host_mac && manual_host.GetHost() == server.discovery_host.host_addr)
            {
                server.manual_host = std::move(manual_host);
                break;
            }
        }
        return server;
    }
    index -= discovered.size();
    if (index < manual.size()) {
        DisplayServer server;
        server.valid = true;
        server.discovered = false;
        server.manual_host = manual.at(index);
        server.registered = false;
        server.duid = QString();
        if (server.manual_host.GetRegistered() && settings->GetRegisteredHostRegistered(server.manual_host.GetMAC())) {
            server.registered = true;
            server.registered_host = settings->GetRegisteredHost(server.manual_host.GetMAC());
        }
        return server;
    }
    index -= manual.size();
    if (index < psn_hosts.count())
    {
        DisplayServer server;

        QMapIterator<QString, PsnHost> i(psn_hosts);
        size_t j = 0;
        while (i.hasNext())
        {
            i.next();
            PsnHost psn_host = i.value();
            bool hidden = false;
            for (const auto &host : discovery_manager.GetHosts())
            {
                if(host.host_name == psn_host.GetName())
                    hidden = true;
            }
            if(hidden)
                continue;
            if(j == index)
            {
                server.valid = true;
                server.discovered = false;
                server.psn_host = std::move(psn_host);
                server.duid = i.key();
                server.registered = true;
                server.registered_host = settings->GetNicknameRegisteredHost(server.psn_host.GetName());
                return server;
            }
            j++;
        }
        return {};
    }
    return {};
}

bool QmlBackend::sendWakeup(const DisplayServer &server)
{
    if (!server.registered)
        return false;
    return sendWakeup(server.GetHostAddr(), server.registered_host.GetRPRegistKey(), server.IsPS5());
}

bool QmlBackend::sendWakeup(const QString &host, const QByteArray &regist_key, bool ps5)
{
    try {
        discovery_manager.SendWakeup(host, regist_key, ps5);
        return true;
    } catch (const Exception &e) {
        emit error(tr("Wakeup failed"), tr("Failed to send Wakeup packet:\n%1").arg(e.what()));
        return false;
    }
}

void QmlBackend::updateControllers()
{
    bool changed = false;
    QMap<QString,QString> controller_mappings = settings->GetControllerMappings();
    for (auto it = controllers.begin(); it != controllers.end();) {
        if (ControllerManager::GetInstance()->GetAvailableControllers().contains(it.key())) {
            it++;
            continue;
        }
        if(it.key() == controller_mapping_id)
        {
            controller_mapping_controller = nullptr;
            controllerMappingQuit();
        }
        QString vidpid = it.value()->GetVIDPID();
        QString guid = it.value()->GetGUID();
        it.value()->deleteLater();
        it = controllers.erase(it);
        changed = true;
    }
    for (auto id : ControllerManager::GetInstance()->GetAvailableControllers()) {
        if (controllers.contains(id))
            continue;
        auto controller = ControllerManager::GetInstance()->OpenController(id);
        if (!controller)
            continue;
        controllers[id] = new QmlController(controller, window, this);
        QString vidpid = controller->GetVIDPIDString();
        QString guid = controller->GetGUIDString();
        QStringList existing_vidpid;
        if(controller_guids_to_update.contains(vidpid))
            existing_vidpid.append(controller_guids_to_update.value(vidpid));
        existing_vidpid.append(guid);
        controller_guids_to_update.insert(vidpid, existing_vidpid);
        // replace old guid with new vid/pid
        if(controller_mappings.contains(guid))
        {
            QString old_mapping = controller_mappings.value(guid);
            qsizetype guid_string_length = old_mapping.indexOf(",") + 1;
            old_mapping.remove(0, guid_string_length);
            settings->RemoveControllerMapping(guid);
            settings->SetControllerMapping(vidpid, old_mapping);
            qCInfo(chiakiGui) << "Migrated controller mapping from platform-specific GUID: " << guid << " to platform-agnostic VID/PID: " << vidpid;
        }
        controller_guids_to_update.insert(vidpid, existing_vidpid);
        connect(controller, &Controller::UpdatingControllerMapping, this, &QmlBackend::controllerMappingUpdate);
        connect(controller, &Controller::NewButtonMapping, this, &QmlBackend::controllerMappingChangeButton);
        changed = true;
    }
    if (changed)
        emit controllersChanged();
}

void QmlBackend::setControllerMappingDefaultMapping(bool is_default_mapping)
{
    if(controller_mapping_default_mapping != is_default_mapping)
    {
        controller_mapping_default_mapping = is_default_mapping;
        emit controllerMappingDefaultMappingChanged();
    }
}

void QmlBackend::setControllerMappingAltered(bool altered)
{
    if(controller_mapping_altered != altered)
    {
        controller_mapping_altered = altered;
        emit controllerMappingAlteredChanged();
    }
}

void QmlBackend::setControllerMappingInProgress(bool is_in_progress)
{
    if(controller_mapping_in_progress != is_in_progress)
    {
        controller_mapping_in_progress = is_in_progress;
        emit controllerMappingInProgressChanged();
    }
}

void QmlBackend::setEnableAnalogStickMapping(bool enabled)
{
    if(enable_analog_stick_mapping != enabled)
    {
        if(controller_mapping_in_progress && controller_mapping_controller)
            controller_mapping_controller->EnableAnalogStickMapping(enabled);
        enable_analog_stick_mapping = enabled;
        emit enableAnalogStickMappingChanged();
    }
}

QVariantList QmlBackend::currentControllerMapping() const
{
    QVariantList out;
    if(!controller_mapping_in_progress)
        return out;
	QMap<int, QStringList> result =
	{
		{CHIAKI_CONTROLLER_BUTTON_CROSS     , controller_mapping_controller_mappings.contains("a") ? controller_mapping_controller_mappings.value("a") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_MOON      , controller_mapping_controller_mappings.contains("b") ? controller_mapping_controller_mappings.value("b") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_BOX       , controller_mapping_controller_mappings.contains("x") ? controller_mapping_controller_mappings.value("x") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_PYRAMID   , controller_mapping_controller_mappings.contains("y") ? controller_mapping_controller_mappings.value("y") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT , controller_mapping_controller_mappings.contains("dpleft") ? controller_mapping_controller_mappings.value("dpleft") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT, controller_mapping_controller_mappings.contains("dpright") ? controller_mapping_controller_mappings.value("dpright") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_UP   , controller_mapping_controller_mappings.contains("dpup") ? controller_mapping_controller_mappings.value("dpup") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN , controller_mapping_controller_mappings.contains("dpdown") ? controller_mapping_controller_mappings.value("dpdown") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_L1        , controller_mapping_controller_mappings.contains("leftshoulder") ? controller_mapping_controller_mappings.value("leftshoulder") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_R1        , controller_mapping_controller_mappings.contains("rightshoulder") ? controller_mapping_controller_mappings.value("rightshoulder") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_L3        , controller_mapping_controller_mappings.contains("leftstick") ? controller_mapping_controller_mappings.value("leftstick") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_R3        , controller_mapping_controller_mappings.contains("rightstick") ? controller_mapping_controller_mappings.value("rightstick") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_OPTIONS   , controller_mapping_controller_mappings.contains("start") ? controller_mapping_controller_mappings.value("start") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_SHARE     , controller_mapping_controller_mappings.contains("back") ? controller_mapping_controller_mappings.value("back") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_TOUCHPAD  , controller_mapping_controller_mappings.contains("touchpad") ? controller_mapping_controller_mappings.value("touchpad") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_PS        , controller_mapping_controller_mappings.contains("guide") ? controller_mapping_controller_mappings.value("guide") : QStringList()},
		{CHIAKI_CONTROLLER_ANALOG_BUTTON_L2 , controller_mapping_controller_mappings.contains("lefttrigger") ? controller_mapping_controller_mappings.value("lefttrigger") : QStringList()},
		{CHIAKI_CONTROLLER_ANALOG_BUTTON_R2 , controller_mapping_controller_mappings.contains("righttrigger") ? controller_mapping_controller_mappings.value("righttrigger") : QStringList()},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X)   , controller_mapping_controller_mappings.contains("leftx") ? controller_mapping_controller_mappings.value("leftx") : QStringList()},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y)   , controller_mapping_controller_mappings.contains("lefty") ? controller_mapping_controller_mappings.value("lefty") : QStringList()},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X)  , controller_mapping_controller_mappings.contains("rightx") ? controller_mapping_controller_mappings.value("rightx") : QStringList()},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y)  , controller_mapping_controller_mappings.contains("righty") ? controller_mapping_controller_mappings.value("righty") : QStringList()},
        {static_cast<int>(ControllerButtonExt::MISC1)   ,      controller_mapping_controller_mappings.contains("misc1") ? controller_mapping_controller_mappings.value("misc1") : QStringList()},
	};

    for (auto it = result.cbegin(); it != result.cend(); ++it) {
        QVariantMap m;
        m["buttonName"] = Settings::GetChiakiControllerButtonName(it.key());
        m["buttonValue"] = it.key();
        m["physicalButton"] = it.value();
        out.append(m);
    }
    return out;
}

void QmlBackend::updateControllerMappings()
{
    if(SDL_WasInit(SDL_INIT_GAMECONTROLLER)==0)
        return;
    QMap<QString,QString> controller_mappings = settings->GetControllerMappings();
    QStringList mapping_vidpids = controller_mappings.keys();
    for(int i=0; i<mapping_vidpids.length(); i++)
    {
        QString vidpid = mapping_vidpids.at(i);
        if(!controller_guids_to_update.contains(vidpid))
            continue;
        QStringList guids_to_update = controller_guids_to_update.value(vidpid);
        for(int j=0; j<guids_to_update.length(); j++)
        {
            QString guid = guids_to_update.at(j);
            if(!controller_mapping_original_controller_mappings.contains(guid))
            {
                const SDL_JoystickGUID real_guid = SDL_JoystickGetGUIDFromString(guid.toUtf8().constData());
                const char *mapping = SDL_GameControllerMappingForGUID(real_guid);
                QString original_controller_mapping(mapping);
                SDL_free((char *)mapping);
                if(original_controller_mapping.isEmpty())
                {
                    qCWarning(chiakiGui) << "Error retrieving controller mapping of GUID " << guid << "with error: " << SDL_GetError();
                    return;
                }
                controller_mapping_original_controller_mappings.insert(guid, original_controller_mapping);
            }
            QString controller_mapping_to_add = controller_mappings.value(vidpid);
            controller_mapping_to_add.prepend(QString("%1,").arg(guid));
            int result = SDL_GameControllerAddMapping(controller_mapping_to_add.toUtf8().constData());
            switch(result)
            {
                case -1:
                    qCWarning(chiakiGui) << "Error setting controller mapping for guid: " << guid << " with error: " << SDL_GetError();
                    break;
                case 0:
                    qCInfo(chiakiGui) << "Updated controller mapping for guid: " << guid;
                    break;
                case 1:
                    qCInfo(chiakiGui) << "Added controller mapping for guid: " << guid;
                    break;
                default:
                    qCInfo(chiakiGui) << "Unidentified problem mapping for guid: " << guid;
                    break;
            }
        }
        controller_guids_to_update.remove(vidpid);
    }
}

void QmlBackend::creatingControllerMapping(bool creating_controller_mapping)
{
    ControllerManager::GetInstance()->creatingControllerMapping(creating_controller_mapping);
}

void QmlBackend::controllerMappingChangeButton(QString button)
{
    if(!controller_mapping_in_progress || !controller_mapping_controller)
        return;
    QStringList mapping_selection = controller_mapping_applied_controller_mappings.value(button);
	QMap<QString, int> button_map =
	{
		{"a", CHIAKI_CONTROLLER_BUTTON_CROSS},
		{"b", CHIAKI_CONTROLLER_BUTTON_MOON},
		{"x", CHIAKI_CONTROLLER_BUTTON_BOX},
		{"y", CHIAKI_CONTROLLER_BUTTON_PYRAMID},
		{"dpleft", CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT},
		{"dpright", CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT},
		{"dpup", CHIAKI_CONTROLLER_BUTTON_DPAD_UP},
		{"dpdown", CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN},
		{"leftshoulder", CHIAKI_CONTROLLER_BUTTON_L1},
		{"rightshoulder", CHIAKI_CONTROLLER_BUTTON_R1},
		{"leftstick", CHIAKI_CONTROLLER_BUTTON_L3},
		{"rightstick", CHIAKI_CONTROLLER_BUTTON_R3},
		{"start", CHIAKI_CONTROLLER_BUTTON_OPTIONS},
		{"back", CHIAKI_CONTROLLER_BUTTON_SHARE},
		{"touchpad", CHIAKI_CONTROLLER_BUTTON_TOUCHPAD},
		{"guide", CHIAKI_CONTROLLER_BUTTON_PS},
		{"lefttrigger", CHIAKI_CONTROLLER_ANALOG_BUTTON_L2},
		{"righttrigger", CHIAKI_CONTROLLER_ANALOG_BUTTON_R2},
		{"leftx", static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X)},
		{"lefty", static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y)},
		{"rightx", static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X)},
		{"righty", static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y)},
        {"misc1", static_cast<int>(ControllerButtonExt::MISC1)},
	};
    int chiaki_button_value = button_map.value(button);
    QString chiaki_button_name = Settings::GetChiakiControllerButtonName(chiaki_button_value);
    emit controllerMappingButtonSelected(std::move(mapping_selection), chiaki_button_value, std::move(chiaki_button_name));
}

void QmlBackend::updateButton(int chiaki_button, QString physical_button, int new_index)
{
	QMap<int, QString> button_map =
	{
		{CHIAKI_CONTROLLER_BUTTON_CROSS     , "a"},
		{CHIAKI_CONTROLLER_BUTTON_MOON      , "b"},
		{CHIAKI_CONTROLLER_BUTTON_BOX       , "x"},
		{CHIAKI_CONTROLLER_BUTTON_PYRAMID   , "y"},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT , "dpleft"},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT, "dpright"},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_UP   , "dpup"},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN , "dpdown"},
		{CHIAKI_CONTROLLER_BUTTON_L1        , "leftshoulder"},
		{CHIAKI_CONTROLLER_BUTTON_R1        , "rightshoulder"},
		{CHIAKI_CONTROLLER_BUTTON_L3        , "leftstick"},
		{CHIAKI_CONTROLLER_BUTTON_R3        , "rightstick"},
		{CHIAKI_CONTROLLER_BUTTON_OPTIONS   , "start"},
		{CHIAKI_CONTROLLER_BUTTON_SHARE     , "back"},
		{CHIAKI_CONTROLLER_BUTTON_TOUCHPAD  , "touchpad"},
		{CHIAKI_CONTROLLER_BUTTON_PS        , "guide"},
		{CHIAKI_CONTROLLER_ANALOG_BUTTON_L2 , "lefttrigger"},
		{CHIAKI_CONTROLLER_ANALOG_BUTTON_R2 , "righttrigger"},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X)   , "leftx"},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y)   , "lefty"},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X)  , "rightx"},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y)  , "righty"},
        {static_cast<int>(ControllerButtonExt::MISC1)   , "misc1"},
	};
    QString button = button_map.value(chiaki_button);
    QString old_mapping = controller_mapping_physical_button_mappings.contains(physical_button) ? controller_mapping_physical_button_mappings.value(physical_button) : QString();
    if(button == old_mapping)
        return;
    if(!old_mapping.isEmpty())
    {
        QStringList old_mapping_buttons = controller_mapping_controller_mappings.value(old_mapping);
        if(old_mapping_buttons.length() > 1)
        {
            old_mapping_buttons.removeOne(physical_button);
            controller_mapping_controller_mappings.insert(old_mapping, old_mapping_buttons);
        }
        else
            controller_mapping_controller_mappings.remove(old_mapping);
    }
    QStringList new_mapping_buttons = controller_mapping_controller_mappings.value(button);
    if(new_mapping_buttons.length() > new_index)
    {
        controller_mapping_physical_button_mappings.insert(new_mapping_buttons.at(new_index), QString());
        new_mapping_buttons.remove(new_index);
    }
    if(new_index == 0)
        new_mapping_buttons.prepend(physical_button);
    else
        new_mapping_buttons.append(physical_button);
    controller_mapping_controller_mappings.insert(button, new_mapping_buttons);
    controller_mapping_physical_button_mappings.insert(physical_button, button);
    if(controller_mapping_controller_mappings == controller_mapping_applied_controller_mappings)
        setControllerMappingAltered(false);
    else
        setControllerMappingAltered(true);
    emit currentControllerMappingChanged();
}

void QmlBackend::controllerMappingUpdate(Controller *controller)
{
    if(SDL_WasInit(SDL_INIT_GAMECONTROLLER)==0)
        return;
    if(controller_mapping_in_progress)
        return;
    controller_mapping_controller = controller;
    if(controller->IsSteamVirtual())
    {
        controllerMappingQuit();
        emit controllerMappingSteamControllerSelected();
        return;
    }
    controller_mapping_id = controller->GetDeviceID();
    const char *mapping = SDL_GameControllerMapping(controller->GetController());
    QString original_controller_mapping(mapping);
    qCInfo(chiakiGui) << "Original controller mapping: " << original_controller_mapping;
    SDL_free((char *)mapping);
    if(original_controller_mapping.isEmpty())
    {
        qCWarning(chiakiGui) << "Error retrieving controller mapping " << SDL_GetError();
        controller_mapping_id = -1;
        controller_mapping_controller = nullptr;
        return;
    }
	QStringList mapping_results = original_controller_mapping.split(u',');
    if(mapping_results.length() < 2)
    {
        qCWarning(chiakiGui) << "Received invalid Mapping List";
        controller_mapping_id = -1;
        controller_mapping_controller = nullptr;
        return;
    }
	controller_mapping_controller_guid = mapping_results.takeFirst();
    controller_mapping_controller_vid_pid = controller_mapping_controller->GetVIDPIDString();
    if(!controller_mapping_original_controller_mappings.contains(controller_mapping_controller_guid))
    {
        setControllerMappingDefaultMapping(true);
        controller_mapping_original_controller_mappings.insert(controller_mapping_controller_guid, original_controller_mapping);
    }
    controller_mapping_controller->EnableAnalogStickMapping(enable_analog_stick_mapping);
	controller_mapping_controller_type = mapping_results.takeFirst();
    if(controller_mapping_controller_type == "*")
    {
        controller_mapping_controller_type = controller_mapping_controller->GetType();
        if(controller_mapping_controller_type.isEmpty())
            controller_mapping_controller_type = QString("Unidentified Controller");
    }
    for(int i=0; i<mapping_results.length(); i++)
    {
        QString individual_mapping = mapping_results.at(i);
        QStringList individual_mapping_list = individual_mapping.split(u':');
        if(individual_mapping_list.length() < 2)
            continue;
        QString key = individual_mapping_list.takeFirst();
        if(controller_mapping_controller_mappings.contains(key))
        {
            auto update_list = controller_mapping_controller_mappings.value(key);
            individual_mapping_list = update_list + individual_mapping_list;
        }
        controller_mapping_controller_mappings.insert(key, individual_mapping_list);
        for(int j = 0; j < individual_mapping_list.length(); j++)
        {
            controller_mapping_physical_button_mappings.insert(individual_mapping_list[j], key);
        }
    }
    QStringList xbox_share_button_controllers;
    xbox_share_button_controllers.append("Xbox One Controller");
    xbox_share_button_controllers.append("Xbox Series X Controller");
    xbox_share_button_controllers.append("Xbox Wireless Controller");
    if((xbox_share_button_controllers.contains(controller_mapping_controller_type)) && !controller_mapping_physical_button_mappings.contains("b11"))
    {
        if(controller_mapping_controller_mappings.contains("misc1"))
        {
            QStringList individual_mapping_list = controller_mapping_controller_mappings.value("misc1");
            individual_mapping_list.append("b11");
            controller_mapping_controller_mappings.insert("misc1", individual_mapping_list);
        }
        else
            controller_mapping_controller_mappings.insert("misc1", QStringList(QString("b11")));
        controller_mapping_physical_button_mappings.insert("b11", "misc1");
        controllerMappingApply();
    }

    controller_mapping_applied_controller_mappings = controller_mapping_controller_mappings;
    emit currentControllerTypeChanged();
    emit currentControllerMappingChanged();
    setControllerMappingInProgress(true);
}

void QmlBackend::controllerMappingSelectButton()
{
    if(controller_mapping_in_progress && controller_mapping_controller)
        controller_mapping_controller->IsUpdatingMappingButton(true);
}

void QmlBackend::controllerMappingReset()
{
    if(!controller_mapping_original_controller_mappings.contains(controller_mapping_controller_guid) || SDL_WasInit(SDL_INIT_GAMECONTROLLER)==0 || !settings->GetControllerMappings().keys().contains(controller_mapping_controller_vid_pid))
    {
        controllerMappingQuit();
        return;
    }
    settings->RemoveControllerMapping(controller_mapping_controller_vid_pid);
    int result = SDL_GameControllerAddMapping(controller_mapping_original_controller_mappings.value(controller_mapping_controller_guid).toUtf8().constData());
    switch(result)
    {
        case -1:
            qCWarning(chiakiGui) << "Error setting controller mapping for guid: " << controller_mapping_controller_guid << " with error: " << SDL_GetError();
            break;
        case 0:
            qCInfo(chiakiGui) << "Updated controller mapping for guid: " << controller_mapping_controller_guid;
            break;
        case 1:
            qCInfo(chiakiGui) << "Added controller mapping for guid: " << controller_mapping_controller_guid;
            break;
        default:
            qCInfo(chiakiGui) << "Unidentified problem mapping for guid: " << controller_mapping_controller_guid;
            break;
    }
    controllerMappingQuit();
}

void QmlBackend::controllerMappingQuit()
{
    if(controller_mapping_in_progress && controller_mapping_controller)
        controller_mapping_controller->IsUpdatingMappingButton(false);
    else
        creatingControllerMapping(false);
    setEnableAnalogStickMapping(false);
    controller_mapping_controller = nullptr;
    setControllerMappingDefaultMapping(false);
    setControllerMappingAltered(false);
    controller_mapping_id = -1;
    controller_mapping_controller_guid.clear();
    controller_mapping_controller_vid_pid.clear();
    controller_mapping_controller_type.clear();
    controller_mapping_controller_mappings.clear();
    controller_mapping_applied_controller_mappings.clear();
    controller_mapping_physical_button_mappings.clear();
    setControllerMappingInProgress(false);
    emit currentControllerTypeChanged();
    emit currentControllerMappingChanged();
}

void QmlBackend::controllerMappingButtonQuit()
{
    if(controller_mapping_in_progress && controller_mapping_controller)
        controller_mapping_controller->IsUpdatingMappingButton(false);
}

void QmlBackend::controllerMappingApply()
{
    QString new_controller_mapping = controller_mapping_controller_type;
    QMapIterator<QString, QStringList> i(controller_mapping_controller_mappings);
    while (i.hasNext()) {
        i.next();
        const auto &physical_buttons = i.value();
        for(int j = 0; j < physical_buttons.length(); j++)
            new_controller_mapping += "," + (i.key() + ":" + physical_buttons.at(j));
    }
    // if user actually reset to default manually, reset mapping, else add
    if(new_controller_mapping == controller_mapping_original_controller_mappings.value(controller_mapping_controller_guid))
    {
        settings->RemoveControllerMapping(controller_mapping_controller_vid_pid);
        int result = SDL_GameControllerAddMapping(controller_mapping_original_controller_mappings.value(controller_mapping_controller_guid).toUtf8().constData());
        switch(result)
        {
            case -1:
                qCWarning(chiakiGui) << "Error setting controller mapping for guid: " << controller_mapping_controller_guid << " with error: " << SDL_GetError();
                break;
            case 0:
                qCInfo(chiakiGui) << "Updated controller mapping for guid: " << controller_mapping_controller_guid;
                break;
            case 1:
                qCInfo(chiakiGui) << "Added controller mapping for guid: " << controller_mapping_controller_guid;
                break;
            default:
                qCInfo(chiakiGui) << "Unidentified problem mapping for guid: " << controller_mapping_controller_guid;
                break;
        }
    }
    else
    {
        QStringList existing_vidpid;
        if(controller_guids_to_update.contains(controller_mapping_controller_vid_pid))
            existing_vidpid.append(controller_guids_to_update.value(controller_mapping_controller_vid_pid));
        existing_vidpid.append(controller_mapping_controller_guid);
        controller_guids_to_update.insert(controller_mapping_controller_vid_pid, existing_vidpid);
        settings->SetControllerMapping(controller_mapping_controller_vid_pid, new_controller_mapping);
    }
}

void QmlBackend::updateDiscoveryHosts()
{
    // Wakeup console that we are currently connecting to
    for (const auto &host : discovery_manager.GetHosts()) {
        if (host.host_addr != session_info.host)
            continue;
        if (host.ps5 != chiaki_target_is_ps5(session_info.target))
            continue;
        if (!settings->GetRegisteredHostRegistered(host.GetHostMAC()))
            continue;
        auto registered = settings->GetRegisteredHost(host.GetHostMAC());
        if (registered.GetRPRegistKey() == session_info.regist_key) {
            if(wakeup_start && session && host.host_name == wakeup_nickname && host.state == CHIAKI_DISCOVERY_HOST_STATE_READY)
            {
                wakeup_nickname.clear();
                wakeup_start = false;
                wakeup_start_timer->stop();
                bool session_start_succeeded = true;

                try {
                    session->Start();
                } catch (const Exception &e) {
                    emit error(tr("Stream failed"), tr("Failed to start Stream Session: %1").arg(e.what()));
                    session_start_succeeded = false;
                    chiaki_log_mutex.lock();
                    chiaki_log_ctx = nullptr;
                    chiaki_log_mutex.unlock();
                    delete session;
                    session = nullptr;
                }
                if(session_start_succeeded)
                {
                    emit sessionChanged(session);
                    sleep_inhibit->inhibit();
                }
            }
            else if(host.state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY && session && session->IsConnecting())
            {
                sendWakeup(host.host_addr, registered.GetRPRegistKey(), host.ps5);
                QString nickname = host.host_name;
                wakeup_nickname = nickname;
                waking_sleeping_nicknames.append(nickname);
                QTimer::singleShot(WAKEUP_PSN_IGNORE_SECONDS * 1000, [this, nickname]{
                    waking_sleeping_nicknames.removeOne(nickname);
                    emit hostsChanged();
                });
                break;
            }
        }
    }
    if (autoConnect()) {
        const int hosts_count = discovery_manager.GetHosts().count();
        for (int i = 0; i < hosts_count; ++i) {
            if (discovery_manager.GetHosts().at(i).GetHostMAC() != auto_connect_mac)
                continue;
            psn_auto_connect_timer->stop();
            connectToHost(i, discovery_manager.GetHosts().at(i).host_name);
            break;
        }
    }
    emit hostsChanged();
}

#if CHIAKI_GUI_ENABLE_STEAM_SHORTCUT
QString QmlBackend::getExecutable() {
#if defined(Q_OS_LINUX)
    //Check for flatpak
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString flatpakId = env.value("FLATPAK_ID");
    QString appImagePath = env.value("APPIMAGE");
    if (!flatpakId.isEmpty()) {
        return QString("flatpak");
    }
    if (!appImagePath.isEmpty())
        return appImagePath;
#endif
    return QCoreApplication::applicationFilePath();
}

void QmlBackend::createSteamShortcut(QString shortcutName, QString launchOptions, const QJSValue &callback)
{
    QJSValue cb = callback;
    QString controller_layout_workshop_id = "3049833406";
    QMap<QString, const QPixmap*> artwork;
    auto landscape = QPixmap(":/icons/steam_landscape.png");
    auto portrait = QPixmap(":/icons/steam_portrait.png");
    QImageReader reader;
    reader.setAllocationLimit(512);
    reader.setFileName(":/icons/steam_hero.png");
    auto hero = QPixmap::fromImageReader(&reader);
    auto icon = QPixmap(":/icons/steam_icon.png");
    auto logo = QPixmap(":/icons/steam_logo.png");
    artwork.insert("landscape", &landscape);
    artwork.insert("portrait", &portrait);
    artwork.insert("hero", &hero);
    artwork.insert("icon", &icon);
    artwork.insert("logo", &logo);
    
    auto infoLambda = [callback](const QString &infoMessage) {
        QJSValue icb = callback;
        if (icb.isCallable())
            icb.call({infoMessage, true, false});
    };

    auto errorLambda = [callback](const QString &errorMessage) {
        QJSValue icb = callback;
        if (icb.isCallable())
            icb.call({errorMessage, false, true});
    };
    SteamTools* steam_tools = new SteamTools(infoLambda, errorLambda);
    bool steamExists = steam_tools->steamExists();
    if(!steamExists)
    {
        if (cb.isCallable())
            cb.call({QString("[E] Steam does not exist, cannot create Steam Shortcut"), false, true});
        delete steam_tools;
        return;
    }

    QString executable = getExecutable();
    if(executable == "flatpak")
    {
        const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        QString flatpakId = env.value("FLATPAK_ID");
        launchOptions.prepend(QString("run %1 ").arg(flatpakId));
    }
    SteamShortcutEntry newShortcut = steam_tools->buildShortcutEntry(std::move(shortcutName), std::move(executable), std::move(launchOptions), std::move(artwork));

    QVector<SteamShortcutEntry> shortcuts = steam_tools->parseShortcuts();
    bool found = false;
    for (auto& map : shortcuts) {
        if (map.getExe() == newShortcut.getExe() && map.getLaunchOptions() == newShortcut.getLaunchOptions()) {
            // Replace the entire map with the new one
            
            if (cb.isCallable())
                cb.call({QString("[I] Updating Steam entry"), true, false});
            map = newShortcut;
            found = true;
            break;  // Stop iterating once a match is found
        }
    }

   //If we didn't find it to update, let's add it to the end
    if (!found) {
        if (cb.isCallable())
            cb.call({QString("[I] Adding Steam entry ") + QString(newShortcut.getAppName().toStdString().c_str()), false, true});
        shortcuts.append(newShortcut);
    }
    steam_tools->updateShortcuts(std::move(shortcuts));
    steam_tools->updateControllerConfig(newShortcut.getAppName(), std::move(controller_layout_workshop_id));
    if (!found)
    {
        if (cb.isCallable())
            cb.call({QString("[I] Added Steam entry: ") + QString(newShortcut.getAppName().toStdString().c_str()), true, true});
    }
    else
    {
        if (cb.isCallable())
            cb.call({QString("[I] Updated Steam entry: ") + QString(newShortcut.getAppName().toStdString().c_str()), true, true});
    }
    delete steam_tools;
}
#endif

QString QmlBackend::openPsnLink()
{
    QUrl url = psnLoginUrl();
    if(QDesktopServices::openUrl(url) && (qEnvironmentVariable("XDG_CURRENT_DESKTOP") != "gamescope"))
    {
        qCWarning(chiakiGui) << "Launched browser.";
        return QString();
    }
    else
    {
        qCWarning(chiakiGui) << "Could not launch browser.";
        return QString(url.toEncoded());
    }
}

QString QmlBackend::openPlaceboOptionsLink()
{
    QUrl url = QUrl("https://libplacebo.org/options/");
    if(QDesktopServices::openUrl(url) && (qEnvironmentVariable("XDG_CURRENT_DESKTOP") != "gamescope"))
    {
        qCWarning(chiakiGui) << "Launched browser.";
        return QString();
    }
    else
    {
        qCWarning(chiakiGui) << "Could not launch browser.";
        return QString(url.toEncoded());
    }
}

bool QmlBackend::checkPsnRedirectURL(const QUrl &url) const
{
    return url.toString().startsWith(QString::fromStdString(PSNAuth::REDIRECT_PAGE));
}

void QmlBackend::initPsnAuth(const QUrl &url, const QJSValue &callback)
{
    const QJSValue cb = callback;
    if (!url.toString().startsWith(QString::fromStdString(PSNAuth::REDIRECT_PAGE)))
    {
        if (cb.isCallable())
            cb.call({QString("[E] Invalid URL: Please make sure you have copy and pasted the URL correctly."), false, true});
        return;
    }
    const QString code = QUrlQuery(url).queryItemValue("code");
    if (code.isEmpty())
    {
        if (cb.isCallable())
            cb.call({QString("[E] Invalid code from redirect url."), false, true});
        return;
    }
    PSNAccountID *psnId = new PSNAccountID(settings, this);
    connect(psnId, &PSNAccountID::AccountIDResponse, this, &QmlBackend::updatePsnHosts);
    connect(psnId, &PSNAccountID::AccountIDError, this, [cb](const QString &error) {
        if (cb.isCallable())
            cb.call({error, false, true});
    });
    connect(psnId, &PSNAccountID::AccountIDResponse, this, [cb]() {
        if (cb.isCallable())
            cb.call({QString("[I] PSN Remote Connection Tokens Generated."), true, true});
    });
    psnId->GetPsnAccountId(code);
    emit psnTokenChanged();
}

void QmlBackend::refreshAuth()
{
    PSNToken *psnToken = new PSNToken(settings, this);
    connect(psnToken, &PSNToken::PSNTokenError, this, [this](const QString &error) {
        qCWarning(chiakiGui) << "Could not refresh token. Automatic PSN Connection Unavailable!" << error;
    });
    connect(psnToken, &PSNToken::UnauthorizedError, this, &QmlBackend::psnCredsExpired);
    connect(psnToken, &PSNToken::PSNTokenSuccess, this, []() {
        qCWarning(chiakiGui) << "PSN Remote Connection Tokens Refreshed.";
    });
    connect(psnToken, &PSNToken::PSNTokenSuccess, this, &QmlBackend::updatePsnHosts);
    QString refresh_token = settings->GetPsnRefreshToken();
    psnToken->RefreshPsnToken(std::move(refresh_token));
}

void QmlBackend::updatePsnHosts()
{
    QString psn_token = settings->GetPsnAuthToken();
    if(psn_token.isEmpty())
        return;

    ChiakiHolepunchDeviceInfo *device_info_ps5 = nullptr;
    size_t num_devices_ps5 = 0;
    ChiakiLog backend_log;
    chiaki_log_init(&backend_log, settings->GetLogLevelMask(), chiaki_log_cb_print, nullptr);
    for(int i = 0; i < PSN_DEVICES_TRIES; i++)
    {
        ChiakiErrorCode err = chiaki_holepunch_list_devices(psn_token.toUtf8().constData(), CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, &device_info_ps5, &num_devices_ps5, &backend_log);
        if (err != CHIAKI_ERR_SUCCESS)
        {
            if(PSN_DEVICES_TRIES - i > 1)
            {
                qCWarning(chiakiGui) << "Failed to get PS5 devices trying again";
                continue;
            }
            else
            {
                qCWarning(chiakiGui) << "Failed to get PS5 devices after max tries: " << PSN_DEVICES_TRIES;
                num_devices_ps5 = 0;
            }
        }
        break;
    }
    for (size_t i = 0; i < num_devices_ps5; i++)
    {
        ChiakiHolepunchDeviceInfo dev = device_info_ps5[i];
        // skip devices that don't have remote play enabled
        if(!dev.remoteplay_enabled)
            continue;
        QByteArray duid_bytes(reinterpret_cast<char*>(dev.device_uid), sizeof(dev.device_uid));
        QString duid = QString(duid_bytes.toHex());
        QString name = QString(dev.device_name);
        if(!settings->GetNicknameRegisteredHostRegistered(name))
            continue;
        bool ps5 = true;
        PsnHost psn_host(duid, name, ps5);
	    if(!psn_hosts.contains(duid))
		    psn_hosts.insert(duid, psn_host);
    }
    if (settings->GetPS4RegisteredHostsRegistered() > 0)
    {
        QByteArray duid_bytes(32, 'A');
        QString duid = QString(duid_bytes.toHex());
        QString name = QString("Main PS4 Console");
        bool ps5 = false;
        PsnHost psn_host(duid, name, ps5);
	    if(!psn_hosts.contains(duid))
		    psn_hosts.insert(duid, psn_host);
    }

    emit hostsChanged();
    qCInfo(chiakiGui) << "Updated PSN hosts";
    if(device_info_ps5)
        chiaki_holepunch_free_device_list(&device_info_ps5);
}

void QmlBackend::refreshPsnToken()
{
    QString expiry_s = settings->GetPsnAuthTokenExpiry();
    QString refresh = settings->GetPsnRefreshToken();
    if(expiry_s.isEmpty() || refresh.isEmpty())
        return;
    QDateTime expiry = QDateTime::fromString(expiry_s, settings->GetTimeFormat());
    // give 1 minute buffer
    QDateTime now = QDateTime::currentDateTime().addSecs(60);
    if (now >= expiry)
        refreshAuth();
    else
        updatePsnHosts();
}

void PsnConnectionWorker::ConnectPsnConnection(StreamSession *session, const QString &duid, const bool &ps5)
{
    ChiakiErrorCode result = session->ConnectPsnConnection(duid, ps5);
    emit resultReady(result);
}
