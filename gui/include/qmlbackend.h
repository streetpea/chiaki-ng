#pragma once

#include "streamsession.h"
#include "discoverymanager.h"
#include "qmlmainwindow.h"
#include "qmlcontroller.h"
#include "qmlsettings.h"

#include <QObject>
#include <QThread>
#include <QJSValue>
#include <QUrl>
#include <QFutureWatcher>

class SystemdInhibit;

class QmlRegist : public QObject
{
    Q_OBJECT

public:
    QmlRegist(const ChiakiRegistInfo &regist_info, uint32_t log_mask, QObject *parent = nullptr);

signals:
    void log(ChiakiLogLevel level, const QString &msg);
    void failed();
    void success(RegisteredHost host);

private:
    static void log_cb(ChiakiLogLevel level, const char *msg, void *user);
    static void regist_cb(ChiakiRegistEvent *event, void *user);

    ChiakiLog chiaki_log;
    ChiakiRegist chiaki_regist;
};

class PsnConnectionWorker : public QObject
{
    Q_OBJECT

public:
    void ConnectPsnConnection(StreamSession *session, const QString &duid, const bool &ps5);

signals:
    void resultReady(const ChiakiErrorCode &err);
};

class QmlBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QmlMainWindow* window READ qmlWindow CONSTANT)
    Q_PROPERTY(QmlSettings* settings READ qmlSettings CONSTANT)
    Q_PROPERTY(StreamSession* session READ qmlSession NOTIFY sessionChanged)
    Q_PROPERTY(QList<QmlController*> controllers READ qmlControllers NOTIFY controllersChanged)
    Q_PROPERTY(bool discoveryEnabled READ discoveryEnabled WRITE setDiscoveryEnabled NOTIFY discoveryEnabledChanged)
    Q_PROPERTY(QVariantList hosts READ hosts NOTIFY hostsChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect NOTIFY autoConnectChanged)
    Q_PROPERTY(PsnConnectState connectState READ connectState WRITE setConnectState NOTIFY connectStateChanged)

public:

    enum class PsnConnectState
    {
        NotStarted,
        WaitingForInternet,
        InitiatingConnection,
        LinkingConsole,
        DataConnectionStart,
        DataConnectionFinished,
        ConnectFailed,
        ConnectFailedStart,
        ConnectFailedConsoleUnreachable,
    };
    Q_ENUM(PsnConnectState);
    QmlBackend(Settings *settings, QmlMainWindow *window);
    ~QmlBackend();

    QmlMainWindow *qmlWindow() const;
    QmlSettings *qmlSettings() const;
    StreamSession *qmlSession() const;
    QList<QmlController*> qmlControllers() const;

    bool discoveryEnabled() const;
    void setDiscoveryEnabled(bool enabled);
    void refreshAuth();

    PsnConnectState connectState() const;
    void setConnectState(PsnConnectState connect_state);

    QVariantList hosts() const;

    bool autoConnect() const;

    void psnConnector();

    void createSession(const StreamSessionConnectInfo &connect_info);

    void psnSessionStart();

    void checkPsnConnection(const ChiakiErrorCode &err);

    void checkNickname(QString nickname);

    bool closeRequested();

    Q_INVOKABLE void deleteHost(int index);
    Q_INVOKABLE void wakeUpHost(int index);
    Q_INVOKABLE void addManualHost(int index, const QString &address);
    Q_INVOKABLE bool registerHost(const QString &host, const QString &psn_id, const QString &pin, const QString &cpin, bool broadcast, int target, const QJSValue &callback);
    Q_INVOKABLE void connectToHost(int index);
    Q_INVOKABLE void stopSession(bool sleep);
    Q_INVOKABLE void sessionGoHome();
    Q_INVOKABLE void enterPin(const QString &pin);
    Q_INVOKABLE QUrl psnLoginUrl() const;
    Q_INVOKABLE bool handlePsnLoginRedirect(const QUrl &url);
    Q_INVOKABLE void stopAutoConnect();
    Q_INVOKABLE void setConsolePin(int index, QString console_pin);
    Q_INVOKABLE QString openPsnLink();
    Q_INVOKABLE void initPsnAuth(const QUrl &url, const QJSValue &callback);
    Q_INVOKABLE void psnCancel(bool stop_thread);
    Q_INVOKABLE void refreshPsnToken();
#if CHIAKI_GUI_ENABLE_STEAM_SHORTCUT
    Q_INVOKABLE void createSteamShortcut(QString shortcutName, QString launchOptions, const QJSValue &callback);
#endif

signals:
    void sessionChanged(StreamSession *session);
    void psnConnect(StreamSession *session, const QString &duid, const bool &ps5);
    void showPsnView();
    void connectStateChanged();
    void controllersChanged();
    void discoveryEnabledChanged();
    void hostsChanged();
    void psnTokenChanged();
    void psnCredsExpired();
    void autoConnectChanged();
    void windowTypeUpdated(WindowType type);

    void error(const QString &title, const QString &text);
    void sessionError(const QString &title, const QString &text);
    void sessionPinDialogRequested();
    void sessionStopDialogRequested();
    void registDialogRequested(const QString &host, bool ps5);
    void psnLoginAccountIdDone(const QString &accountId);

private:
    struct DisplayServer {
        bool valid = false;

        DiscoveryHost discovery_host;
        ManualHost manual_host;
        bool discovered;

        PsnHost psn_host;
        QString duid;
        RegisteredHost registered_host;
        bool registered;

        QString GetHostAddr() const { return discovered ? discovery_host.host_addr : manual_host.GetHost(); }
        bool IsPS5() const { return discovered ? discovery_host.ps5 :
            (registered ? chiaki_target_is_ps5(registered_host.GetTarget()) : false); }
    };

    DisplayServer displayServerAt(int index) const;
    bool sendWakeup(const DisplayServer &server);
    bool sendWakeup(const QString &host, const QByteArray &regist_key, bool ps5);
    void updateControllers();
    void updateDiscoveryHosts();
    void updatePsnHosts();
    QString getExecutable();

    Settings *settings = {};
    QmlSettings *settings_qml = {};
    QmlMainWindow *window = {};
    StreamSession *session = {};
    QThread *frame_thread = {};
    QTimer *psn_reconnect_timer = {};
    QTimer *psn_auto_connect_timer = {};
    int psn_reconnect_tries = 0;
    QThread psn_connection_thread;
    PsnConnectState psn_connect_state;
    DiscoveryManager discovery_manager;
    QHash<int, QmlController*> controllers;
    DisplayServer regist_dialog_server;
    StreamSessionConnectInfo session_info = {};
    SystemdInhibit *sleep_inhibit = {};
    bool resume_session = false;
    HostMAC auto_connect_mac = {};
    QString auto_connect_nickname = "";
    QMap<QString, PsnHost> psn_hosts;
};