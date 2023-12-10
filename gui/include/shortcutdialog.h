#ifndef SHORTCUTDIALOG_H
#define SHORTCUTDIALOG_H
#include <QDialog>
#include <mainwindow.h>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>

class ShortcutDialog : public QDialog{
    private:
        const DisplayServer* server;

        QCheckBox *allow_external_access_checkbox;
        QLineEdit *external_dns_edit;
        QLineEdit *local_ssid_edit;
        QComboBox *mode_combo_box;
        QLineEdit *passcode_edit;

        QPushButton *create_shortcut_button;


    private slots:
        void ExternalChanged();
        void CreateShortcut();

    public:
        ShortcutDialog(const DisplayServer *server, QWidget *parent = nullptr);
        std::string compileTemplate(const std::string& templateFile, const std::map<std::string, std::string>& inputMap);
        std::string getConnectedSSID();
};



#endif //SHORTCUTDIALOG_H
