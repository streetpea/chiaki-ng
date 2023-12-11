#ifndef SHORTCUTDIALOG_H
#define SHORTCUTDIALOG_H
#include <QDialog>
#include <mainwindow.h>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <qeventloop.h>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "ui_shortcutdialog.h"

enum class ArtworkType {
    LANDSCAPE,
    PORTRAIT,
    HERO,
    LOGO,
    ICON
};

enum class RotateDirection {
    NEXT,
    PREV
};

class ShortcutDialog : public QDialog, private Ui::ShortcutDialog{
    Q_OBJECT
    private:
        const DisplayServer* server;

        std::string customLandscape;
        std::string customPortrait;
        std::string customIcon;
        std::string customHero;
        std::string customLogo;

        std::vector<std::string> landscapes;
        std::vector<std::string> portraits;
        std::vector<std::string> icons;
        std::vector<std::string> heroes;
        std::vector<std::string> logos;

        int landscapeIndex;
        int portraitIndex;
        int iconIndex;
        int heroIndex;
        int logoIndex;

    private slots:
        void ExternalChanged();
        void CreateShortcut(const DisplayServer* server, std::map<std::string, std::string> artwork);
        void UpdateImageList(ArtworkType artworkType, std::vector<std::string>& images, std::string gameId, std::string& custom, QPushButton* prev, QPushButton* next);
        void RotateImage(RotateDirection direction, QLabel* label, std::vector<std::string>& images, int& index, std::string type);

    public:
        ShortcutDialog(const DisplayServer *server, QWidget *parent = nullptr);
        std::string compileTemplate(const std::string& templateFile, const std::map<std::string, std::string>& inputMap);
        std::string getConnectedSSID();
        static void AddToSteam(const DisplayServer* server, std::string filePath, std::map<std::string, std::string> artwork);
        static void loadImage(QLabel* label, std::vector<std::string> images, int index);
};

#endif //SHORTCUTDIALOG_H
