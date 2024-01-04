#ifndef SHORTCUTDIALOG_H
#define SHORTCUTDIALOG_H
#include <mainwindow.h>
#include <qeventloop.h>
#include <QNetworkReply>
#include "ui_shortcutdialog.h"
#include "settings.h"
#include "sgdbartworkwidget.h"
#include "steamtools.h"

class ShortcutDialog : public QDialog, private Ui::ShortcutDialog {
    Q_OBJECT

private:
    SteamTools* steamTools;
    const DisplayServer* server;
    ChiakiLog log;
    QMap<QString, const QPixmap*> artwork;

private slots:
    void ExternalChanged();
    void CreateShortcut(const DisplayServer* server);
    #ifdef CHIAKI_ENABLE_ARTWORK
        void updateArtwork(QMap<QString, const QPixmap*> artwork);
    #endif


public:

    ShortcutDialog(Settings* settings, const DisplayServer* server, QWidget* parent = nullptr);
    QString getExecutable();
    QString compileTemplate(const QString& templateFile, const QMap<QString,QString>& inputMap);
    QString getConnectedSSID();
    void AddToSteam(const DisplayServer* server, QString filePath);
};

#endif //SHORTCUTDIALOG_H
