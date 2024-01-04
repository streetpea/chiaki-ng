#ifndef STEAMSHORTCUTENTRY_H
#define STEAMSHORTCUTENTRY_H
#include <QMap>
#include <QObject>

class SteamShortcutEntry {

    private:
        QMap<QString, QString> properties;

    public:
        explicit SteamShortcutEntry();
        void setProperty(QString key, QString value);

        QString getAppid();
        QString getAppName();
        QString getExe();
        QString getStartDir();
        QString geticon();
        QString getShortcutPath();
        QString getLaunchOptions();
        QString getIsHidden();
        QString getAllowDesktopConfig();
        QString getAllowOverlay();
        QString getOpenVR();
        QString getDevkit();
        QString getDevkitGameID();
        QString getDevkitOverrideAppID();
        QString getLastPlayTime();
        QString getFlatpakAppID();

};

#endif //STEAMSHORTCUTENTRY_H
