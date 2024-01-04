#include "steamshortcutentry.h"

SteamShortcutEntry::SteamShortcutEntry() {
}

void SteamShortcutEntry::setProperty(QString key, QString value) {
    properties.insert(key, value);
}

QString SteamShortcutEntry::getAppid() {
    return properties.value("appid");
}
QString SteamShortcutEntry::getAppName() {
    return properties.value("AppName");
}
QString SteamShortcutEntry::getExe() {
    return properties.value("Exe");
}
QString SteamShortcutEntry::getStartDir() {
    return properties.value("StartDir");
}
QString SteamShortcutEntry::geticon() {
    return properties.value("icon");
}
QString SteamShortcutEntry::getShortcutPath() {
    return properties.value("ShortcutPath");
}
QString SteamShortcutEntry::getLaunchOptions() {
    return properties.value("LaunchOptions");
}
QString SteamShortcutEntry::getIsHidden() {
    return properties.value("IsHidden");
}
QString SteamShortcutEntry::getAllowDesktopConfig() {
    return properties.value("AllowDesktopConfig");
}
QString SteamShortcutEntry::getAllowOverlay() {
    return properties.value("AllowOverlay");
}
QString SteamShortcutEntry::getOpenVR() {
    return properties.value("OpenVR");
}
QString SteamShortcutEntry::getDevkit() {
    return properties.value("Devkit");
}
QString SteamShortcutEntry::getDevkitGameID() {
    return properties.value("DevkitGameID");
}
QString SteamShortcutEntry::getDevkitOverrideAppID() {
    return properties.value("DevkitOverrideAppID");
}
QString SteamShortcutEntry::getLastPlayTime() {
    return properties.value("LastPlayTime");
}
QString SteamShortcutEntry::getFlatpakAppID() {
    return properties.value("FlatpakAppID");
}
