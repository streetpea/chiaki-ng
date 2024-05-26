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
#include <QProcessEnvironment>
#include <QDesktopServices>
#include <QtConcurrent>

#define PSN_DEVICES_TRIES 2
#define MAX_PSN_RECONNECT_TRIES 6
#define PSN_INTERNET_WAIT_SECONDS 5
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

    const char *uri = "org.streetpea.chiaki4deck";
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
    connect(settings, &Settings::ManualHostsUpdated, this, &QmlBackend::hostsChanged);
    connect(&discovery_manager, &DiscoveryManager::HostsUpdated, this, &QmlBackend::updateDiscoveryHosts);
    discovery_manager.SetSettings(settings);
    setDiscoveryEnabled(true);

    connect(ControllerManager::GetInstance(), &ControllerManager::AvailableControllersUpdated, this, &QmlBackend::updateControllers);
    updateControllers();

    auto_connect_mac = settings->GetAutoConnectHost().GetServerMAC();
    auto_connect_nickname = settings->GetAutoConnectHost().GetServerNickname();
    psn_auto_connect_timer = new QTimer(this);
    psn_auto_connect_timer->setSingleShot(true);
    psn_reconnect_tries = 0;
    psn_reconnect_timer = new QTimer(this);
    if(autoConnect() && !auto_connect_nickname.isEmpty())
    {
        connect(psn_auto_connect_timer, &QTimer::timeout, this, [this, settings]
        {
            int i = 0;
            for (const auto &host : std::as_const(psn_hosts))
            {
                if(host.GetName() == auto_connect_nickname)
                {
                    int index = discovery_manager.GetHosts().size() + settings->GetManualHosts().size() + i;
                    connectToHost(index);
                    return;
                }
                i++;
            }
            qCWarning(chiakiGui) << "Couldn't find PSN host with the requested nickname: " << auto_connect_nickname;
        });
        psn_auto_connect_timer->start(PSN_INTERNET_WAIT_SECONDS * 1000);
    }
    connect(psn_reconnect_timer, &QTimer::timeout, this, [this, settings]{
        QString refresh = settings->GetPsnRefreshToken();
        if(refresh.isEmpty())
        {
            qCWarning(chiakiGui) << "No refresh token found, can't refresh PSN token to use PSN remote connection";
            psn_reconnect_tries = 0;
            resume_session = false;
            psn_reconnect_timer->stop();
            setConnectState(PsnConnectState::ConnectFailed);
            return;
        }
        PSNToken *psnToken = new PSNToken(settings, this);
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
        QString refresh_token = settings->GetPsnRefreshToken();
        psnToken->RefreshPsnToken(refresh_token);
    });
    sleep_inhibit = new SystemdInhibit(QGuiApplication::applicationName(), tr("Remote Play session"), "sleep", "delay", this);
    connect(sleep_inhibit, &SystemdInhibit::sleep, this, [this]() {
        qCInfo(chiakiGui) << "About to sleep";
        if (session) {
            if (this->settings->GetSuspendAction() == SuspendAction::Sleep)
                session->GoToBed();
            session->Stop();
            psnCancel(true);
            resume_session = true;
        }
    });
    connect(sleep_inhibit, &SystemdInhibit::resume, this, [this, settings]() {
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
    frame_thread->quit();
    frame_thread->wait();
    delete frame_thread->parent();
    delete psn_auto_connect_timer;
    delete psn_reconnect_timer;
    psn_connection_thread.quit();
    psn_connection_thread.wait();
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
    size_t registered_discovered_ps4s = 0;
    for (const auto &host : discovery_manager.GetHosts()) {
        QVariantMap m;
        bool registered = settings->GetRegisteredHostRegistered(host.GetHostMAC());
        m["discovered"] = true;
        m["manual"] = false;
        m["name"] = host.host_name;
        m["duid"] = "";
        m["address"] = host.host_addr;
        m["ps5"] = host.ps5;
        m["mac"] = host.GetHostMAC().ToString();
        m["state"] = chiaki_discovery_host_state_string(host.state);
        m["app"] = host.running_app_name;
        m["titleId"] = host.running_app_titleid;
        m["registered"] = registered;
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
        m["registered"] = false;
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
        if(discovered)
            continue;
        m["discovered"] = false;
        m["manual"] = false;
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
                delete session;
                session = NULL;
            }
            break;
        case CHIAKI_ERR_HOST_UNREACH:
            setConnectState(PsnConnectState::ConnectFailedConsoleUnreachable);
            if(session)
            {
                delete session;
                session = NULL;
            }
            break;
        default:
            setConnectState(PsnConnectState::ConnectFailed);
            if(session)
            {
                delete session;
                session = NULL;
            }
            break;
    }
}

void QmlBackend::psnSessionStart()
{
    session->Start();

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
    if (session_info.hw_decoder == "vulkan") {
        session_info.hw_device_ctx = window->vulkanHwDeviceCtx();
        if (!session_info.hw_device_ctx)
            session_info.hw_decoder.clear();
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
        if (frame->hw_frames_ctx && !zero_copy_formats.contains(frame->format)) {
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
        window->resize(connect_info.video_profile.width, connect_info.video_profile.height);

    chiaki_log_mutex.lock();
    chiaki_log_ctx = session->GetChiakiLog();
    chiaki_log_mutex.unlock();

    if(connect_info.duid.isEmpty())
    {
        session->Start();
        emit sessionChanged(session);

        sleep_inhibit->inhibit();
    }
    else
    {
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
    if (!server.valid || server.discovered)
        return;
    settings->RemoveManualHost(server.manual_host.GetID());
}

void QmlBackend::wakeUpHost(int index)
{
    auto server = displayServerAt(index);
    if (!server.valid)
        return;
    sendWakeup(server);
}

void QmlBackend::setConsolePin(int index, QString console_pin)
{
    auto server = displayServerAt(index);
    if (!server.valid)
        return;
    server.registered_host.SetConsolePin(server.registered_host, console_pin);
    settings->AddRegisteredHost(server.registered_host);
}

void QmlBackend::addManualHost(int index, const QString &address)
{
    HostMAC hmac;
    if (index >= 0) {
        auto server = displayServerAt(index);
        if (!server.valid)
            return;
        hmac = server.registered_host.GetServerMAC();
    }
    ManualHost host(-1, address, index >= 0, hmac);
    settings->SetManualHost(host);
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
    connect(regist, &QmlRegist::success, this, [this, host, callback](RegisteredHost rhost) {
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

void QmlBackend::connectToHost(int index)
{
    auto server = displayServerAt(index);
    if (!server.valid)
        return;

    if (!server.registered) {
        regist_dialog_server = server;
        emit registDialogRequested(server.GetHostAddr(), server.IsPS5());
        return;
    }

    if (server.discovered && server.discovery_host.state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY && !sendWakeup(server))
        return;

    bool fullscreen = false, zoom = false, stretch = false;
    switch (settings->GetWindowType()) {
    case WindowType::SelectedResolution:
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
                host,
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
            psnToken->RefreshPsnToken(refresh_token);
        }
        else
            createSession(info);
    }
}

void QmlBackend::stopSession(bool sleep)
{
    if (!session)
        return;

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
    size_t duid_size = 48;
    char duid[duid_size];
    chiaki_holepunch_generate_client_device_uid(duid, &duid_size);
    return QUrl(PSNAuth::LOGIN_URL + "duid=" + QString(duid) + "&");
}

bool QmlBackend::handlePsnLoginRedirect(const QUrl &url)
{
    if (!url.toString().startsWith(QString::fromStdString(PSNAuth::REDIRECT_PAGE)))
        return false;

    const QString code = QUrlQuery(url).queryItemValue("code");
    if (code.isEmpty()) {
        qCWarning(chiakiGui) << "Invalid code from redirect url";
        emit psnLoginAccountIdDone({});
        return false;
    }
    PSNAccountID *psnId = new PSNAccountID(settings, this);
    connect(psnId, &PSNAccountID::AccountIDResponse, this, [this, psnId](const QString &accountId) {
        psnId->deleteLater();
        emit psnLoginAccountIdDone(accountId);
    });
    connect(psnId, &PSNAccountID::AccountIDResponse, this, &QmlBackend::updatePsnHosts);
    connect(psnId, &PSNAccountID::AccountIDError, [this](const QString &error) {
        qCWarning(chiakiGui) << "Could not refresh token. Automatic PSN Connection Unavailable!" << error;
    });
    psnId->GetPsnAccountId(code);
    emit psnTokenChanged();
    return true;
}

void QmlBackend::stopAutoConnect()
{
    auto_connect_mac = {};
    emit autoConnectChanged();
}

QmlBackend::DisplayServer QmlBackend::displayServerAt(int index) const
{
    if (index < 0)
        return {};
    auto discovered = discovery_manager.GetHosts();
    if (index < discovered.size()) {
        DisplayServer server;
        server.valid = true;
        server.discovered = true;
        server.discovery_host = discovered.at(index);
        server.registered = settings->GetRegisteredHostRegistered(server.discovery_host.GetHostMAC());
        server.duid = QString();
        if (server.registered)
            server.registered_host = settings->GetRegisteredHost(server.discovery_host.GetHostMAC());
        return server;
    }
    index -= discovered.size();
    auto manual = settings->GetManualHosts();
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
                server.psn_host = psn_host;
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
    for (auto it = controllers.begin(); it != controllers.end();) {
        if (ControllerManager::GetInstance()->GetAvailableControllers().contains(it.key())) {
            it++;
            continue;
        }
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
        changed = true;
    }
    if (changed)
        emit controllersChanged();
}

void QmlBackend::updateDiscoveryHosts()
{
    if (session && session->IsConnecting()) {
        // Wakeup console that we are currently connecting to
        for (auto host : discovery_manager.GetHosts()) {
            if (host.state != CHIAKI_DISCOVERY_HOST_STATE_STANDBY)
                continue;
            if (host.host_addr != session_info.host)
                continue;
            if (host.ps5 != chiaki_target_is_ps5(session_info.target))
                continue;
            if (!settings->GetRegisteredHostRegistered(host.GetHostMAC()))
                continue;
            auto registered = settings->GetRegisteredHost(host.GetHostMAC());
            if (registered.GetRPRegistKey() == session_info.regist_key) {
                sendWakeup(host.host_addr, registered.GetRPRegistKey(), host.ps5);
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
            connectToHost(i);
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
    auto hero = QPixmap(":/icons/steam_hero.png");
    auto icon = QPixmap(":/icons/steam_icon.png");
    auto logo = QPixmap(":/icons/steam_logo.png");
    artwork.insert("landscape", &landscape);
    artwork.insert("portrait", &portrait);
    artwork.insert("hero", &hero);
    artwork.insert("icon", &icon);
    artwork.insert("logo", &logo);
    
    auto infoLambda = [this, callback](const QString &infoMessage) {
        QJSValue icb = callback;
        if (icb.isCallable())
            icb.call({infoMessage, true, false});
    };

    auto errorLambda = [this, callback](const QString &errorMessage) {
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
        return;
    }

    QString executable = getExecutable();
    if(executable == "flatpak")
    {
        const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        QString flatpakId = env.value("FLATPAK_ID");
        launchOptions.prepend(QString("run %1 ").arg(flatpakId));
    }
    SteamShortcutEntry newShortcut = steam_tools->buildShortcutEntry(shortcutName, executable, launchOptions, artwork);

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
    steam_tools->updateShortcuts(shortcuts);
    steam_tools->updateControllerConfig(newShortcut.getAppName(), controller_layout_workshop_id);
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
}
#endif

QString QmlBackend::openPsnLink()
{
    size_t duid_size = CHIAKI_DUID_STR_SIZE;
    char duid[duid_size];
    chiaki_holepunch_generate_client_device_uid(duid, &duid_size);
    QUrl url = QUrl(PSNAuth::LOGIN_URL + "duid=" + QString(duid) + "&");
    if(QDesktopServices::openUrl(url))
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

void QmlBackend::initPsnAuth(const QUrl &url, const QJSValue &callback)
{
    QJSValue cb = callback;
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
    connect(psnId, &PSNAccountID::AccountIDError, [this, cb](const QString &error) {
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
    psnToken->RefreshPsnToken(refresh_token);
}

void QmlBackend::updatePsnHosts()
{
    QString psn_token = settings->GetPsnAuthToken();
    if(psn_token.isEmpty())
        return;

    ChiakiHolepunchDeviceInfo *device_info_ps5 = NULL;
    size_t num_devices_ps5 = 0;
    ChiakiLog backend_log;
    chiaki_log_init(&backend_log, settings->GetLogLevelMask(), chiaki_log_cb_print, NULL);
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
