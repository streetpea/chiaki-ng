/**
 * This class assists with:
 *  - Checking if Steam is installed
 *  - Reading a shortcuts.vdf file
 *  - Creating new shortcut entries
 *  - Writing entries back to shortcuts.vdf
 *  - Setting the neptune controller config for a shortcut
 *
 *  Logic for creating the APP-ID for use in shortcuts.vdf is based upon Steam Rom Manager. Reference code found here:
 *  https://github.com/SteamGridDB/steam-rom-manager/blob/ed718710bdc2cb14527f801b82afb8c8353efb40/src/lib/helpers/steam/generate-app-id.ts#L2
 */
#ifndef STEAMTOOLS_H
#define STEAMTOOLS_H
#include <QObject>
#include <QMap>

#include "mainwindow.h"
#include "steamshortcutentry.h"
#include "chiaki/log.h"

class SteamTools : public QObject {
    Q_OBJECT

    public:
    SteamTools(const std::function<void(const QString&)>& infoFunction, const std::function<void(const QString&)>& errorFunction);

        bool steamExists();
        QVector<SteamShortcutEntry> parseShortcuts();
        SteamShortcutEntry buildShortcutEntry(QString appName, QString filepath, QMap<QString, const QPixmap*> artwork);
        void updateShortcuts(QVector<SteamShortcutEntry> shortcuts);
        void updateControllerConfig(QString appname, QString controllerConfigID);

    private:
        QString steamBaseDir;
        QString mostRecentUser;
        QString shortcutFile;

        std::function<void(const QString&)> infoFunction;
        std::function<void(const QString&)> errorFunction;
        QString getSteamBaseDir();
        QString getMostRecentUser();
        QString getShortcutFile();
        QString generateShortAppId(QString exe, QString appname);
        uint32_t generateShortcutId(QString exe, QString appname);
        void saveArtwork(QString shortAppId, QMap<QString, const QPixmap*> artwork, QMap<QString, QString> artworkLocations);

};


#endif //STEAMTOOLS_H