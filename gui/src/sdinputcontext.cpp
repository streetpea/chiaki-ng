#include "sdinputcontext.h"

#include <fcntl.h>
#include <unistd.h>

#include <QDir>
#include <QDebug>
#include <QGuiApplication>
#include <QInputMethodQueryEvent>

QPlatformInputContext *SDInputContextPlugin::create(const QString &key, const QStringList &paramList)
{
    if (key.compare(QStringLiteral("sdinput"), Qt::CaseInsensitive) == 0)
        return new SDInputContext;
    return nullptr;
}

SDInputContext::SDInputContext()
{
}

bool SDInputContext::isValid() const
{
    return true;
}

static bool runSteamUrl(const QString &url)
{
    const QString file_name = QDir::home().filePath(".steam/steam.pipe");
    int fd = open(qPrintable(file_name), O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        qWarning() << "Failed to open" << file_name << "for writing";
        return false;
    }
    QByteArray buf = url.toUtf8();
    buf.append('\n');
    write(fd, buf.constData(), buf.size());
    close(fd);
    return true;
}

void SDInputContext::showInputPanel()
{
    const QString url = QStringLiteral("steam://open/keyboard?Mode=0");
    if (runSteamUrl(url)) {
        is_visible = true;
        emitInputPanelVisibleChanged();
    }
}

void SDInputContext::hideInputPanel()
{
    const QString url = QStringLiteral("steam://close/keyboard");
    if (runSteamUrl(url)) {
        is_visible = false;
        emitInputPanelVisibleChanged();
    }
}

bool SDInputContext::isInputPanelVisible() const
{
    return is_visible;
}

void SDInputContext::setFocusObject(QObject *object)
{
    if (!is_visible)
        return;

    if (!object) {
        hideInputPanel();
        return;
    }

    QInputMethodQueryEvent ev(Qt::ImEnabled);
    QGuiApplication::sendEvent(object, &ev);
    if (!ev.value(Qt::ImEnabled).toBool())
        hideInputPanel();
}
