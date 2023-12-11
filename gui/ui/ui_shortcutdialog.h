/********************************************************************************
** Form generated from reading UI file 'shortcutdialog.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_SHORTCUTDIALOG_H
#define UI_SHORTCUTDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_ShortcutDialog
{
public:
    QGroupBox *shortcut_settings_group;
    QWidget *shortcut_settings_form;
    QFormLayout *formLayout;
    QLabel *allow_external_access_label;
    QCheckBox *allow_external_access_checkbox;
    QLabel *external_dns_label;
    QLineEdit *external_dns_edit;
    QLabel *local_ssid_label;
    QLineEdit *local_ssid_edit;
    QComboBox *mode_combo_box;
    QLabel *mode_combo_box_label;
    QLineEdit *passcode_edit;
    QLabel *passcode_label;
    QGroupBox *artwork_group_box;
    QTabWidget *artwork_tabs;
    QWidget *landscapeTab;
    QWidget *landscape_verticalLayoutWidget;
    QVBoxLayout *landscape_verticalLayout;
    QComboBox *landscape_game_combo;
    QHBoxLayout *landscape_horizontalLayout_2;
    QLabel *landscape_label;
    QHBoxLayout *landscape_horizontalLayout;
    QPushButton *landscape_prev_button;
    QPushButton *landscape_upload_button;
    QPushButton *landscape_next_button;
    QWidget *portraitTab;
    QWidget *portrait_verticalLayoutWidget;
    QVBoxLayout *portrait_verticalLayout;
    QComboBox *portrait_game_combo;
    QHBoxLayout *portrait_horizontalLayout_2;
    QLabel *portrait_label;
    QHBoxLayout *portrait_horizontalLayout;
    QPushButton *portrait_prev_button;
    QPushButton *portrait_upload_button;
    QPushButton *portrait_next_button;
    QWidget *heroTab;
    QWidget *hero_verticalLayoutWidget;
    QVBoxLayout *hero_verticalLayout;
    QComboBox *hero_game_combo;
    QHBoxLayout *hero_horizontalLayout_2;
    QLabel *hero_label;
    QHBoxLayout *hero_horizontalLayout;
    QPushButton *hero_prev_button;
    QPushButton *hero_upload_button;
    QPushButton *hero_next_button;
    QWidget *iconTab;
    QWidget *icon_verticalLayoutWidget;
    QVBoxLayout *icon_verticalLayout;
    QComboBox *icon_game_combo;
    QHBoxLayout *icon_horizontalLayout_2;
    QLabel *icon_label;
    QHBoxLayout *icon_horizontalLayout;
    QPushButton *icon_prev_button;
    QPushButton *icon_upload_button;
    QPushButton *icon_next_button;
    QWidget *logoTab;
    QWidget *logo_verticalLayoutWidget;
    QVBoxLayout *logo_verticalLayout;
    QComboBox *logo_game_combo;
    QHBoxLayout *logo_horizontalLayout_2;
    QLabel *logo_label;
    QHBoxLayout *logo_horizontalLayout;
    QPushButton *logo_prev_button;
    QPushButton *logo_upload_button;
    QPushButton *logo_next_button;
    QPushButton *add_to_steam_button;

    void setupUi(QDialog *ShortcutDialog)
    {
        if (ShortcutDialog->objectName().isEmpty())
            ShortcutDialog->setObjectName(QString::fromUtf8("ShortcutDialog"));
        ShortcutDialog->resize(550, 580);
        shortcut_settings_group = new QGroupBox(ShortcutDialog);
        shortcut_settings_group->setObjectName(QString::fromUtf8("shortcut_settings_group"));
        shortcut_settings_group->setGeometry(QRect(10, 0, 530, 180));
        shortcut_settings_form = new QWidget(shortcut_settings_group);
        shortcut_settings_form->setObjectName(QString::fromUtf8("shortcut_settings_form"));
        shortcut_settings_form->setGeometry(QRect(0, 20, 530, 160));
        formLayout = new QFormLayout(shortcut_settings_form);
        formLayout->setObjectName(QString::fromUtf8("formLayout"));
        formLayout->setVerticalSpacing(-1);
        formLayout->setContentsMargins(0, 0, 0, 0);
        allow_external_access_label = new QLabel(shortcut_settings_form);
        allow_external_access_label->setObjectName(QString::fromUtf8("allow_external_access_label"));
        allow_external_access_label->setMaximumSize(QSize(16777215, 16777215));

        formLayout->setWidget(0, QFormLayout::LabelRole, allow_external_access_label);

        allow_external_access_checkbox = new QCheckBox(shortcut_settings_form);
        allow_external_access_checkbox->setObjectName(QString::fromUtf8("allow_external_access_checkbox"));

        formLayout->setWidget(0, QFormLayout::FieldRole, allow_external_access_checkbox);

        external_dns_label = new QLabel(shortcut_settings_form);
        external_dns_label->setObjectName(QString::fromUtf8("external_dns_label"));

        formLayout->setWidget(1, QFormLayout::LabelRole, external_dns_label);

        external_dns_edit = new QLineEdit(shortcut_settings_form);
        external_dns_edit->setObjectName(QString::fromUtf8("external_dns_edit"));
        external_dns_edit->setEnabled(false);

        formLayout->setWidget(1, QFormLayout::FieldRole, external_dns_edit);

        local_ssid_label = new QLabel(shortcut_settings_form);
        local_ssid_label->setObjectName(QString::fromUtf8("local_ssid_label"));

        formLayout->setWidget(2, QFormLayout::LabelRole, local_ssid_label);

        local_ssid_edit = new QLineEdit(shortcut_settings_form);
        local_ssid_edit->setObjectName(QString::fromUtf8("local_ssid_edit"));
        local_ssid_edit->setEnabled(false);

        formLayout->setWidget(2, QFormLayout::FieldRole, local_ssid_edit);

        mode_combo_box = new QComboBox(shortcut_settings_form);
        mode_combo_box->setObjectName(QString::fromUtf8("mode_combo_box"));

        formLayout->setWidget(3, QFormLayout::FieldRole, mode_combo_box);

        mode_combo_box_label = new QLabel(shortcut_settings_form);
        mode_combo_box_label->setObjectName(QString::fromUtf8("mode_combo_box_label"));

        formLayout->setWidget(3, QFormLayout::LabelRole, mode_combo_box_label);

        passcode_edit = new QLineEdit(shortcut_settings_form);
        passcode_edit->setObjectName(QString::fromUtf8("passcode_edit"));

        formLayout->setWidget(4, QFormLayout::FieldRole, passcode_edit);

        passcode_label = new QLabel(shortcut_settings_form);
        passcode_label->setObjectName(QString::fromUtf8("passcode_label"));

        formLayout->setWidget(4, QFormLayout::LabelRole, passcode_label);

        artwork_group_box = new QGroupBox(ShortcutDialog);
        artwork_group_box->setObjectName(QString::fromUtf8("artwork_group_box"));
        artwork_group_box->setGeometry(QRect(10, 190, 530, 350));
        artwork_tabs = new QTabWidget(artwork_group_box);
        artwork_tabs->setObjectName(QString::fromUtf8("artwork_tabs"));
        artwork_tabs->setGeometry(QRect(20, 30, 490, 300));
        landscapeTab = new QWidget();
        landscapeTab->setObjectName(QString::fromUtf8("landscapeTab"));
        landscape_verticalLayoutWidget = new QWidget(landscapeTab);
        landscape_verticalLayoutWidget->setObjectName(QString::fromUtf8("landscape_verticalLayoutWidget"));
        landscape_verticalLayoutWidget->setGeometry(QRect(10, 10, 470, 280));
        landscape_verticalLayout = new QVBoxLayout(landscape_verticalLayoutWidget);
        landscape_verticalLayout->setObjectName(QString::fromUtf8("landscape_verticalLayout"));
        landscape_verticalLayout->setContentsMargins(0, 0, 0, 0);
        landscape_game_combo = new QComboBox(landscape_verticalLayoutWidget);
        landscape_game_combo->setObjectName(QString::fromUtf8("landscape_game_combo"));

        landscape_verticalLayout->addWidget(landscape_game_combo);

        landscape_horizontalLayout_2 = new QHBoxLayout();
        landscape_horizontalLayout_2->setObjectName(QString::fromUtf8("landscape_horizontalLayout_2"));
        landscape_label = new QLabel(landscape_verticalLayoutWidget);
        landscape_label->setObjectName(QString::fromUtf8("landscape_label"));
        landscape_label->setMaximumSize(QSize(200, 94));
        landscape_label->setScaledContents(true);

        landscape_horizontalLayout_2->addWidget(landscape_label);


        landscape_verticalLayout->addLayout(landscape_horizontalLayout_2);

        landscape_horizontalLayout = new QHBoxLayout();
        landscape_horizontalLayout->setObjectName(QString::fromUtf8("landscape_horizontalLayout"));
        landscape_prev_button = new QPushButton(landscape_verticalLayoutWidget);
        landscape_prev_button->setObjectName(QString::fromUtf8("landscape_prev_button"));

        landscape_horizontalLayout->addWidget(landscape_prev_button);

        landscape_upload_button = new QPushButton(landscape_verticalLayoutWidget);
        landscape_upload_button->setObjectName(QString::fromUtf8("landscape_upload_button"));

        landscape_horizontalLayout->addWidget(landscape_upload_button);

        landscape_next_button = new QPushButton(landscape_verticalLayoutWidget);
        landscape_next_button->setObjectName(QString::fromUtf8("landscape_next_button"));

        landscape_horizontalLayout->addWidget(landscape_next_button);


        landscape_verticalLayout->addLayout(landscape_horizontalLayout);

        artwork_tabs->addTab(landscapeTab, QString());
        portraitTab = new QWidget();
        portraitTab->setObjectName(QString::fromUtf8("portraitTab"));
        portrait_verticalLayoutWidget = new QWidget(portraitTab);
        portrait_verticalLayoutWidget->setObjectName(QString::fromUtf8("portrait_verticalLayoutWidget"));
        portrait_verticalLayoutWidget->setGeometry(QRect(10, 10, 470, 280));
        portrait_verticalLayout = new QVBoxLayout(portrait_verticalLayoutWidget);
        portrait_verticalLayout->setObjectName(QString::fromUtf8("portrait_verticalLayout"));
        portrait_verticalLayout->setContentsMargins(0, 0, 0, 0);
        portrait_game_combo = new QComboBox(portrait_verticalLayoutWidget);
        portrait_game_combo->setObjectName(QString::fromUtf8("portrait_game_combo"));

        portrait_verticalLayout->addWidget(portrait_game_combo);

        portrait_horizontalLayout_2 = new QHBoxLayout();
        portrait_horizontalLayout_2->setObjectName(QString::fromUtf8("portrait_horizontalLayout_2"));
        portrait_label = new QLabel(portrait_verticalLayoutWidget);
        portrait_label->setObjectName(QString::fromUtf8("portrait_label"));
        portrait_label->setMaximumSize(QSize(94, 140));
        portrait_label->setScaledContents(true);

        portrait_horizontalLayout_2->addWidget(portrait_label);


        portrait_verticalLayout->addLayout(portrait_horizontalLayout_2);

        portrait_horizontalLayout = new QHBoxLayout();
        portrait_horizontalLayout->setObjectName(QString::fromUtf8("portrait_horizontalLayout"));
        portrait_prev_button = new QPushButton(portrait_verticalLayoutWidget);
        portrait_prev_button->setObjectName(QString::fromUtf8("portrait_prev_button"));

        portrait_horizontalLayout->addWidget(portrait_prev_button);

        portrait_upload_button = new QPushButton(portrait_verticalLayoutWidget);
        portrait_upload_button->setObjectName(QString::fromUtf8("portrait_upload_button"));

        portrait_horizontalLayout->addWidget(portrait_upload_button);

        portrait_next_button = new QPushButton(portrait_verticalLayoutWidget);
        portrait_next_button->setObjectName(QString::fromUtf8("portrait_next_button"));

        portrait_horizontalLayout->addWidget(portrait_next_button);


        portrait_verticalLayout->addLayout(portrait_horizontalLayout);

        artwork_tabs->addTab(portraitTab, QString());
        heroTab = new QWidget();
        heroTab->setObjectName(QString::fromUtf8("heroTab"));
        hero_verticalLayoutWidget = new QWidget(heroTab);
        hero_verticalLayoutWidget->setObjectName(QString::fromUtf8("hero_verticalLayoutWidget"));
        hero_verticalLayoutWidget->setGeometry(QRect(10, 10, 470, 280));
        hero_verticalLayout = new QVBoxLayout(hero_verticalLayoutWidget);
        hero_verticalLayout->setObjectName(QString::fromUtf8("hero_verticalLayout"));
        hero_verticalLayout->setContentsMargins(0, 0, 0, 0);
        hero_game_combo = new QComboBox(hero_verticalLayoutWidget);
        hero_game_combo->setObjectName(QString::fromUtf8("hero_game_combo"));

        hero_verticalLayout->addWidget(hero_game_combo);

        hero_horizontalLayout_2 = new QHBoxLayout();
        hero_horizontalLayout_2->setObjectName(QString::fromUtf8("hero_horizontalLayout_2"));
        hero_label = new QLabel(hero_verticalLayoutWidget);
        hero_label->setObjectName(QString::fromUtf8("hero_label"));
        hero_label->setMaximumSize(QSize(200, 94));
        hero_label->setScaledContents(true);

        hero_horizontalLayout_2->addWidget(hero_label);


        hero_verticalLayout->addLayout(hero_horizontalLayout_2);

        hero_horizontalLayout = new QHBoxLayout();
        hero_horizontalLayout->setObjectName(QString::fromUtf8("hero_horizontalLayout"));
        hero_prev_button = new QPushButton(hero_verticalLayoutWidget);
        hero_prev_button->setObjectName(QString::fromUtf8("hero_prev_button"));

        hero_horizontalLayout->addWidget(hero_prev_button);

        hero_upload_button = new QPushButton(hero_verticalLayoutWidget);
        hero_upload_button->setObjectName(QString::fromUtf8("hero_upload_button"));

        hero_horizontalLayout->addWidget(hero_upload_button);

        hero_next_button = new QPushButton(hero_verticalLayoutWidget);
        hero_next_button->setObjectName(QString::fromUtf8("hero_next_button"));

        hero_horizontalLayout->addWidget(hero_next_button);


        hero_verticalLayout->addLayout(hero_horizontalLayout);

        artwork_tabs->addTab(heroTab, QString());
        iconTab = new QWidget();
        iconTab->setObjectName(QString::fromUtf8("iconTab"));
        icon_verticalLayoutWidget = new QWidget(iconTab);
        icon_verticalLayoutWidget->setObjectName(QString::fromUtf8("icon_verticalLayoutWidget"));
        icon_verticalLayoutWidget->setGeometry(QRect(10, 10, 470, 280));
        icon_verticalLayout = new QVBoxLayout(icon_verticalLayoutWidget);
        icon_verticalLayout->setObjectName(QString::fromUtf8("icon_verticalLayout"));
        icon_verticalLayout->setContentsMargins(0, 0, 0, 0);
        icon_game_combo = new QComboBox(icon_verticalLayoutWidget);
        icon_game_combo->setObjectName(QString::fromUtf8("icon_game_combo"));

        icon_verticalLayout->addWidget(icon_game_combo);

        icon_horizontalLayout_2 = new QHBoxLayout();
        icon_horizontalLayout_2->setObjectName(QString::fromUtf8("icon_horizontalLayout_2"));
        icon_label = new QLabel(icon_verticalLayoutWidget);
        icon_label->setObjectName(QString::fromUtf8("icon_label"));
        icon_label->setMaximumSize(QSize(94, 94));
        icon_label->setScaledContents(true);

        icon_horizontalLayout_2->addWidget(icon_label);


        icon_verticalLayout->addLayout(icon_horizontalLayout_2);

        icon_horizontalLayout = new QHBoxLayout();
        icon_horizontalLayout->setObjectName(QString::fromUtf8("icon_horizontalLayout"));
        icon_prev_button = new QPushButton(icon_verticalLayoutWidget);
        icon_prev_button->setObjectName(QString::fromUtf8("icon_prev_button"));

        icon_horizontalLayout->addWidget(icon_prev_button);

        icon_upload_button = new QPushButton(icon_verticalLayoutWidget);
        icon_upload_button->setObjectName(QString::fromUtf8("icon_upload_button"));

        icon_horizontalLayout->addWidget(icon_upload_button);

        icon_next_button = new QPushButton(icon_verticalLayoutWidget);
        icon_next_button->setObjectName(QString::fromUtf8("icon_next_button"));

        icon_horizontalLayout->addWidget(icon_next_button);


        icon_verticalLayout->addLayout(icon_horizontalLayout);

        artwork_tabs->addTab(iconTab, QString());
        logoTab = new QWidget();
        logoTab->setObjectName(QString::fromUtf8("logoTab"));
        logo_verticalLayoutWidget = new QWidget(logoTab);
        logo_verticalLayoutWidget->setObjectName(QString::fromUtf8("logo_verticalLayoutWidget"));
        logo_verticalLayoutWidget->setGeometry(QRect(10, 10, 470, 280));
        logo_verticalLayout = new QVBoxLayout(logo_verticalLayoutWidget);
        logo_verticalLayout->setObjectName(QString::fromUtf8("logo_verticalLayout"));
        logo_verticalLayout->setContentsMargins(0, 0, 0, 0);
        logo_game_combo = new QComboBox(logo_verticalLayoutWidget);
        logo_game_combo->setObjectName(QString::fromUtf8("logo_game_combo"));

        logo_verticalLayout->addWidget(logo_game_combo);

        logo_horizontalLayout_2 = new QHBoxLayout();
        logo_horizontalLayout_2->setObjectName(QString::fromUtf8("logo_horizontalLayout_2"));
        logo_label = new QLabel(logo_verticalLayoutWidget);
        logo_label->setObjectName(QString::fromUtf8("logo_label"));
        logo_label->setMaximumSize(QSize(200, 94));
        logo_label->setScaledContents(true);

        logo_horizontalLayout_2->addWidget(logo_label);


        logo_verticalLayout->addLayout(logo_horizontalLayout_2);

        logo_horizontalLayout = new QHBoxLayout();
        logo_horizontalLayout->setObjectName(QString::fromUtf8("logo_horizontalLayout"));
        logo_prev_button = new QPushButton(logo_verticalLayoutWidget);
        logo_prev_button->setObjectName(QString::fromUtf8("logo_prev_button"));

        logo_horizontalLayout->addWidget(logo_prev_button);

        logo_upload_button = new QPushButton(logo_verticalLayoutWidget);
        logo_upload_button->setObjectName(QString::fromUtf8("logo_upload_button"));

        logo_horizontalLayout->addWidget(logo_upload_button);

        logo_next_button = new QPushButton(logo_verticalLayoutWidget);
        logo_next_button->setObjectName(QString::fromUtf8("logo_next_button"));

        logo_horizontalLayout->addWidget(logo_next_button);


        logo_verticalLayout->addLayout(logo_horizontalLayout);

        artwork_tabs->addTab(logoTab, QString());
        add_to_steam_button = new QPushButton(ShortcutDialog);
        add_to_steam_button->setObjectName(QString::fromUtf8("add_to_steam_button"));
        add_to_steam_button->setGeometry(QRect(20, 540, 510, 32));

        retranslateUi(ShortcutDialog);

        artwork_tabs->setCurrentIndex(0);


        QMetaObject::connectSlotsByName(ShortcutDialog);
    } // setupUi

    void retranslateUi(QDialog *ShortcutDialog)
    {
        ShortcutDialog->setWindowTitle(QCoreApplication::translate("ShortcutDialog", "Add to Steam", nullptr));
        shortcut_settings_group->setTitle(QCoreApplication::translate("ShortcutDialog", "Shortcut Settings", nullptr));
        allow_external_access_label->setText(QCoreApplication::translate("ShortcutDialog", "Access Externally?", nullptr));
        external_dns_label->setText(QCoreApplication::translate("ShortcutDialog", "External IP Address or DNS Entry", nullptr));
        local_ssid_label->setText(QCoreApplication::translate("ShortcutDialog", "What's the SSID of your home wifi?", nullptr));
        mode_combo_box_label->setText(QCoreApplication::translate("ShortcutDialog", "Screen Mode", nullptr));
        passcode_label->setText(QCoreApplication::translate("ShortcutDialog", "Pincode (Optional)", nullptr));
        artwork_group_box->setTitle(QCoreApplication::translate("ShortcutDialog", "Artwork", nullptr));
        landscape_prev_button->setText(QCoreApplication::translate("ShortcutDialog", "Previous Image", nullptr));
        landscape_upload_button->setText(QCoreApplication::translate("ShortcutDialog", "Upload Image", nullptr));
        landscape_next_button->setText(QCoreApplication::translate("ShortcutDialog", "Next Image", nullptr));
        artwork_tabs->setTabText(artwork_tabs->indexOf(landscapeTab), QCoreApplication::translate("ShortcutDialog", "Landscape", nullptr));
        portrait_prev_button->setText(QCoreApplication::translate("ShortcutDialog", "Previous Image", nullptr));
        portrait_upload_button->setText(QCoreApplication::translate("ShortcutDialog", "Upload Image", nullptr));
        portrait_next_button->setText(QCoreApplication::translate("ShortcutDialog", "Next Image", nullptr));
        artwork_tabs->setTabText(artwork_tabs->indexOf(portraitTab), QCoreApplication::translate("ShortcutDialog", "Portrait", nullptr));
        hero_prev_button->setText(QCoreApplication::translate("ShortcutDialog", "Previous Image", nullptr));
        hero_upload_button->setText(QCoreApplication::translate("ShortcutDialog", "Upload Image", nullptr));
        hero_next_button->setText(QCoreApplication::translate("ShortcutDialog", "Next Image", nullptr));
        artwork_tabs->setTabText(artwork_tabs->indexOf(heroTab), QCoreApplication::translate("ShortcutDialog", "Hero", nullptr));
        icon_prev_button->setText(QCoreApplication::translate("ShortcutDialog", "Previous Image", nullptr));
        icon_upload_button->setText(QCoreApplication::translate("ShortcutDialog", "Upload Image", nullptr));
        icon_next_button->setText(QCoreApplication::translate("ShortcutDialog", "Next Image", nullptr));
        artwork_tabs->setTabText(artwork_tabs->indexOf(iconTab), QCoreApplication::translate("ShortcutDialog", "Icon", nullptr));
        logo_prev_button->setText(QCoreApplication::translate("ShortcutDialog", "Previous Image", nullptr));
        logo_upload_button->setText(QCoreApplication::translate("ShortcutDialog", "Upload Image", nullptr));
        logo_next_button->setText(QCoreApplication::translate("ShortcutDialog", "Next Image", nullptr));
        artwork_tabs->setTabText(artwork_tabs->indexOf(logoTab), QCoreApplication::translate("ShortcutDialog", "Logo", nullptr));
        add_to_steam_button->setText(QCoreApplication::translate("ShortcutDialog", "Add to Steam", nullptr));
    } // retranslateUi

};

namespace Ui {
    class ShortcutDialog: public Ui_ShortcutDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_SHORTCUTDIALOG_H
