//
// Created by Jamie Bartlett on 04/12/2023.
//

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

class ShortcutDialog : public QDialog{
    Q_OBJECT
    private:
    const DisplayServer* server;

    QCheckBox *allow_external_access_checkbox;
    QLineEdit *external_dns_edit;
    QLineEdit *local_ssid_edit;
    QComboBox *mode_combo_box;
    QLineEdit *passcode_edit;
    QGroupBox *external_group_box;

    QPushButton *create_shortcut_button;

    QLabel *landscapeLabel;
    QLabel *portraitLabel;
    QLabel *iconLabel;
    QLabel *heroLabel;
    QLabel *logoLabel;

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
    void CreateShortcut(std::map<std::string, std::string> artwork);
    void RotateImage(QLabel* label, std::vector<std::string>& images, int& index, std::string type);

public:
    ShortcutDialog(const DisplayServer *server, QWidget *parent = nullptr);
    std::string compileTemplate(const std::string& templateFile, const std::map<std::string, std::string>& inputMap);
    std::string getConnectedSSID();
    static void AddToSteam(const DisplayServer* server, std::string filePath, std::map<std::string, std::string> artwork);
    static void loadImage(QLabel* label, std::vector<std::string> images, int index);
};

#endif //SHORTCUTDIALOG_H
