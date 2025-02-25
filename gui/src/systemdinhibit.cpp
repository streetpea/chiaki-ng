#include "systemdinhibit.h"

#ifdef CHIAKI_HAVE_DBUS
#include <fcntl.h>
#include <unistd.h>
#include <QDBusMessage>
#include <QDBusConnection>
#include <QDBusPendingReply>
#include <QDBusPendingCallWatcher>
#include <QDBusUnixFileDescriptor>
#endif

SystemdInhibit::SystemdInhibit(const QString &who, const QString &why, const QString &what, const QString &mode, QObject *parent)
    : QObject(parent)
    , who(who)
    , why(why)
    , what(what)
    , mode(mode)
{
#ifdef CHIAKI_HAVE_DBUS
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.login1"),
                                         QStringLiteral("/org/freedesktop/login1"),
                                         QStringLiteral("org.freedesktop.login1.Manager"),
                                         QStringLiteral("PrepareForSleep"),
                                         this,
                                         SIGNAL(login1PrepareForSleep(bool)));
#endif
}

void SystemdInhibit::inhibit()
{
#ifdef CHIAKI_HAVE_DBUS
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
                                                       QStringLiteral("/org/freedesktop/login1"),
                                                       QStringLiteral("org.freedesktop.login1.Manager"),
                                                       QStringLiteral("Inhibit"));
    call << what << who << why << mode;

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        watcher->deleteLater();
        const QDBusPendingReply<QDBusUnixFileDescriptor> reply = *watcher;
        if (reply.isError()) {
            qWarning() << "Inhibit Error:" << reply.error().name() << reply.error().message();
        } else {
            fd = reply.value().fileDescriptor();
            if (fd == -1)
                qWarning() << "Received invalid fd";
            else
                fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        }
    });
#endif
}

void SystemdInhibit::simulateUserActivity()
{
#ifdef CHIAKI_HAVE_DBUS
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.ScreenSaver"),
                                                       QStringLiteral("/ScreenSaver"),
                                                       QStringLiteral("org.freedesktop.ScreenSaver"),
                                                       QStringLiteral("SimulateUserActivity"));
    call;

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        watcher->deleteLater();
        const QDBusPendingReply<void> reply = *watcher;
        if (reply.isError())
            qWarning() << "Simulate User Activity Error:" << reply.error().name() << reply.error().message();
    });
#endif
}

void SystemdInhibit::login1PrepareForSleep(bool start)
{
    if (start)
        emit sleep();
    else
        emit resume();
}

void SystemdInhibit::release()
{
#ifdef CHIAKI_HAVE_DBUS
    if (fd >= 0)
        close(fd);
    fd = -1;
#endif
}
