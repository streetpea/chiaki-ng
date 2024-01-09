#ifndef SHORTCUTDIALOG_H
#define SHORTCUTDIALOG_H
#include <mainwindow.h>
#include <qeventloop.h>
#include "settings.h"
#include "sgdbartworkwidget.h"
#include "steamtools.h"

class ShortcutDialog : public QDialog {
    Q_OBJECT

private:
    QComboBox *mode_combo_box;
    QLineEdit *passcode_edit;
#ifdef CHIAKI_ENABLE_ARTWORK
    QPushButton *choose_artwork_button;
#endif
    QPushButton *add_to_steam_button;

    SteamTools* steamTools;
    const DisplayServer* server;
    ChiakiLog log;
    QMap<QString, const QPixmap*> artwork;

private slots:
    void CreateShortcut(const DisplayServer* server);
    #ifdef CHIAKI_ENABLE_ARTWORK
        void updateArtwork(QMap<QString, const QPixmap*> artwork);
    #endif


public:

    ShortcutDialog(Settings* settings, const DisplayServer* server, QWidget* parent = nullptr);
    QString getExecutable();
    QString getLaunchOptions(const DisplayServer* displayServer);
    void AddToSteam(const DisplayServer* server);
};

#endif //SHORTCUTDIALOG_H
