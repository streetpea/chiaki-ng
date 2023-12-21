#pragma once

#include "streamsession.h"
#include "discoverymanager.h"
#include "qmlmainwindow.h"
#include "qmlcontroller.h"

#include <QObject>
#include <QThread>
#include <QJSValue>

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

class QmlBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QmlMainWindow* window READ qmlWindow CONSTANT)
    Q_PROPERTY(StreamSession* session READ qmlSession NOTIFY sessionChanged)
    Q_PROPERTY(QList<QmlController*> controllers READ qmlControllers NOTIFY controllersChanged)
    Q_PROPERTY(bool discoveryEnabled READ discoveryEnabled WRITE setDiscoveryEnabled NOTIFY discoveryEnabledChanged)
    Q_PROPERTY(QVariantList hosts READ hosts NOTIFY hostsChanged)

public:
    QmlBackend(Settings *settings, QmlMainWindow *window);
    ~QmlBackend();

    QmlMainWindow *qmlWindow() const;
    StreamSession *qmlSession() const;
    QList<QmlController*> qmlControllers() const;

    bool discoveryEnabled() const;
    void setDiscoveryEnabled(bool enabled);

    QVariantList hosts() const;

    void createSession(const StreamSessionConnectInfo &connect_info);

    bool closeRequested();

    Q_INVOKABLE void deleteHost(int index);
    Q_INVOKABLE void wakeUpHost(int index);
    Q_INVOKABLE void addManualHost(int index, const QString &address);
    Q_INVOKABLE bool registerHost(const QString &host, const QString &psn_id, const QString &pin, bool broadcast, int target, const QJSValue &callback);
    Q_INVOKABLE void connectToHost(int index);
    Q_INVOKABLE void stopSession(bool sleep);

    Q_INVOKABLE void showSettingsDialog();

signals:
    void sessionChanged(StreamSession *session);
    void controllersChanged();
    void discoveryEnabledChanged();
    void hostsChanged();

    void error(const QString &title, const QString &text);
    void sessionError(const QString &title, const QString &text);
    void sessionStopDialogRequested();
    void registDialogRequested(const QString &host, bool ps5);

private:
    struct DisplayServer {
        bool valid = false;

        DiscoveryHost discovery_host;
        ManualHost manual_host;
        bool discovered;

        RegisteredHost registered_host;
        bool registered;

        QString GetHostAddr() const { return discovered ? discovery_host.host_addr : manual_host.GetHost(); }
        bool IsPS5() const { return discovered ? discovery_host.ps5 :
            (registered ? chiaki_target_is_ps5(registered_host.GetTarget()) : false); }
    };

    DisplayServer displayServerAt(int index) const;
    void sendWakeup(const DisplayServer &server);
    void showLoginPINDialog(bool incorrect);
    void updateControllers();

    Settings *settings = {};
    QmlMainWindow *window = {};
    StreamSession *session = {};
    QThread *frame_thread = {};
    DiscoveryManager discovery_manager;
    QHash<int, QmlController*> controllers;
    DisplayServer regist_dialog_server;
};
