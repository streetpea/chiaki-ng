#ifndef SHORTCUTDIALOG_H
#define SHORTCUTDIALOG_H
#include <mainwindow.h>
#include <qeventloop.h>
#include <QNetworkReply>
#include "ui_shortcutdialog.h"
#include "settings.h"
#include "sgdbartworkwidget.h"

class ShortcutDialog : public QDialog, private Ui::ShortcutDialog {
    Q_OBJECT

private:
    const DisplayServer* server;
    ChiakiLog log;
    QMap<ArtworkType, SGDBArtworkWidget*> artworkWidgets;

private slots:
    void ExternalChanged();

    void CreateShortcut(const DisplayServer* server, std::map<std::string, std::string> artwork);

public:
    ShortcutDialog(Settings* settings, const DisplayServer* server, QWidget* parent = nullptr);

    std::string getExecutable();

    std::string compileTemplate(const std::string& templateFile, const std::map<std::string, std::string>& inputMap);

    std::string getConnectedSSID();

    void AddToSteam(const DisplayServer* server, std::string filePath, std::map<std::string, std::string> artwork);
};

#endif //SHORTCUTDIALOG_H
