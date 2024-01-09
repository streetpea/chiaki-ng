#include "shortcutdialog.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <qeventloop.h>
#include <QFileDialog>
#include <QMessageBox>
#include <qtextstream.h>
#include <regex>
#include <sstream>
#include <string>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <QFormLayout>
#include <QInputDialog>
#include <QStandardPaths>
#include <QtCore/qprocess.h>

#include "autoconnecthelper.h"
#include "imageloader.h"

#ifdef CHIAKI_ENABLE_ARTWORK
#include "artworkdialog.h"
#endif

QString controller_layout_workshop_id = "3049833406";

ShortcutDialog::ShortcutDialog(Settings *settings, const DisplayServer *server, QWidget* parent) : settings(settings) {
    chiaki_log_init(&log, settings->GetLogLevelMask(), chiaki_log_cb_print, this);

    //Create steam tools
    auto infoLambda = [this](const QString &infoMessage) {
        CHIAKI_LOGI(&log, infoMessage.toStdString().c_str());
    };

    auto errorLambda = [this](const QString &errorMessage) {
        CHIAKI_LOGE(&log, errorMessage.toStdString().c_str());
    };
    steamTools = new SteamTools(infoLambda, errorLambda);

    setWindowTitle("Add to Steam");

    auto layout = new QVBoxLayout(this);
    setLayout(layout);

    auto group_box = new QGroupBox(tr("Shortcut Settings"));
    layout->addWidget(group_box);

    auto form_layout = new QFormLayout(this);
    group_box->setLayout(form_layout);

    //Screen Mode combo
    mode_combo_box = new QComboBox(this);
    static const QList<QPair<ChiakiScreenModePreset, const char *>> mode_strings = {
        { CHIAKI_MODE_STRETCH, "stretch" },
        { CHIAKI_MODE_ZOOM, "zoom" },
        { CHIAKI_MODE_FULLSCREEN, "fullscreen" }
    };
    for(const auto &p : mode_strings)
    {
        mode_combo_box->addItem(p.second, p.first);
    }
    form_layout->addRow(tr("Screen mode"), mode_combo_box);

    passcode_edit = new QLineEdit(this);
    form_layout->addRow(tr("Pincode (Optional)"), passcode_edit);



    #ifdef CHIAKI_ENABLE_ARTWORK
        //Artwork
        choose_artwork_button = new QPushButton(tr("Choose Artwork"));
        form_layout->addRow(choose_artwork_button);
        connect(choose_artwork_button, &QPushButton::clicked, [this]() {
            auto dialog = new ArtworkDialog(this);
            connect(dialog, &ArtworkDialog::updateArtwork, this, &ShortcutDialog::updateArtwork);
            dialog->exec();
        });
    #endif

    //Shortcut Button
    add_to_steam_button = new QPushButton(tr("Add to Steam"));
    layout->addWidget(add_to_steam_button);
    connect(add_to_steam_button, &QPushButton::clicked, [=]() {
        CreateShortcut(server);
    });
}

QString ShortcutDialog::getExecutable() {
    QString executable = AutoConnectHelper::GetExecutable();
#if defined(__linux)
    //Check for flatpak
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString flatpakId = env.value("FLATPAK_ID");
    if (!flatpakId.isEmpty()) {
        return AutoConnectHelper::RunShellCommand("which flatpak", false);
    }
#endif
    return executable;
}

QString ShortcutDialog::getLaunchOptions(const DisplayServer* displayServer) {
    QString launchOptions;
    if (mode_combo_box->currentData() == CHIAKI_MODE_ZOOM) {
        launchOptions.append("--zoom");
    } else if (mode_combo_box->currentData() == CHIAKI_MODE_STRETCH) {
        launchOptions.append("--stretch");
    } else if (mode_combo_box->currentData() == CHIAKI_MODE_FULLSCREEN) {
        launchOptions.append("--fullscreen");
    }
    if (passcode_edit->text() != "") {
        launchOptions.append(" --passcode="+passcode_edit->text());
    }

    launchOptions.append(QString(" autoconnect %1").arg(displayServer->registered_host.GetServerNickname()));

#if defined(__linux)
    //Check for flatpak
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString flatpakId = env.value("FLATPAK_ID");
    if (!flatpakId.isEmpty()) {
        launchOptions.prepend(QString("run --branch=stable --arch=x86_64 --command=chiaki %1 ").arg(flatpakId));
    }
#endif

    return launchOptions;
}

/**
 * \brief Given a DisplayServer object, create an executable shell script based on
 * templates from resources.qrc
 *
 * If Steam is found, this executable is created in the Chiaki config directory and added to steam
 * If Steam is not found the user is given a choice of where to save the executable and it is NOT added to steam.
 *
 * \param displayServer The chosen Server item.
 */
void ShortcutDialog::CreateShortcut(const DisplayServer* displayServer) {
    //Check if we have any local SSIDs, without them, it won't work
    QStringList local_ssids = settings->GetLocalSSIDs();
    if (local_ssids.isEmpty()) {
        int addLocalSSID = QMessageBox::critical(nullptr, "No Local SSID set", QString::fromStdString("You've not set any local SSIDs. Without these Chiaki will try to connect remotely. It's likely the remote address also isn't set. Add a local SSID now?"), QMessageBox::Yes, QMessageBox::No);
        if (addLocalSSID == QMessageBox::Yes) {
            QString currentSSID = AutoConnectHelper::GetCurrentSSID();
            QString new_ssid = QInputDialog::getText(nullptr, "New Local SSID", "Name of Local SSID:", QLineEdit::Normal, currentSSID);
            if (!new_ssid.isEmpty()) {
                settings->AddLocalSSID(new_ssid);
            } else {
               int continueWithoutAdding = QMessageBox::critical(nullptr, "No Local SSID still not set", QString::fromStdString("You still don't have a local SSID, are you sure you want to continue?"), QMessageBox::Yes, QMessageBox::No);
                if (continueWithoutAdding == QMessageBox::No) {
                    return;
                }
            }
        }
    }

    bool steamExists = steamTools->steamExists();

    //If Steam not found, ask the user where to save.
    QString filePath;
    if (!steamExists) {
        int createShortcut = QMessageBox::critical(nullptr, "Steam not found", QString::fromStdString("Steam not found! Save shortcut elsewhere?"), QMessageBox::Ok, QMessageBox::Cancel);
        if (createShortcut == QMessageBox::Cancel) {
            return;
        }
        QString filePath = QFileDialog::getSaveFileName(nullptr, "Save Shell Script", QDir::homePath() + QDir::separator() + displayServer->registered_host.GetServerNickname() + ".sh", "Shell Scripts (*.sh)");

        // Open the file for writing
        QFile outputFile(filePath);
        if (outputFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            // Write the string to the file
            QTextStream out(&outputFile);
            out << QString("%1\n%2 %3\n")
                .arg("#!/usr/bin/env bash")
                .arg(getExecutable())
                .arg(getLaunchOptions(displayServer));

            //Set permissions
            outputFile.setPermissions(QFile::ExeOwner | QFile::ReadOwner | QFile::WriteOwner |
                QFile::ExeOther | QFile::ReadOther | QFile::WriteOther);

            // Close the file
            outputFile.close();

            QMessageBox::information(nullptr, "Success", QString("Created shortcut for %1.").arg(displayServer->registered_host.GetServerNickname()), QMessageBox::Ok);
            close();
        }

        return;
    }

    AddToSteam(displayServer);
    QMessageBox::information(nullptr, "Success", QString("Added %1 to Steam. Please restart Steam.").arg(displayServer->registered_host.GetServerNickname()), QMessageBox::Ok);
    close();
}

#ifdef CHIAKI_ENABLE_ARTWORK
/**
 * \brief Update the artwork map with the response from the ArtworkDialog
 * \param returnedArtwork Artwork returned from Artwork Dialog
 */
void ShortcutDialog::updateArtwork(QMap<QString, const QPixmap*> returnedArtwork) {
    artwork = returnedArtwork;
    choose_artwork_button->setText("Artwork Selected");
}
#endif

/**
 * \brief Given a DisplayServer object and a path to an executable, add this executable to Steam.
 * First check if it already exists, if so, update this entry instead.
 *
 * Once added, update the Steam controller config for this record.
 *
 * \param server
 * \param filePath
 */
void ShortcutDialog::AddToSteam(const DisplayServer* server) {
    //Get Shortcut options
    QString executable = getExecutable();
    QString launchOptions = getLaunchOptions(server);

    //Get the current shortcuts
    QVector<SteamShortcutEntry> shortcuts = steamTools->parseShortcuts();

    const QString serverNickname = server->registered_host.GetServerNickname();

    //Create a new shortcut entry
    SteamShortcutEntry newShortcut = steamTools->buildShortcutEntry(serverNickname, executable, launchOptions, artwork);

    bool found = false;
    for (auto& map : shortcuts) {
        if (map.getExe() == newShortcut.getExe()) {
            // Replace the entire map with the new one

            CHIAKI_LOGI(&log, "Updating Steam entry");
            map = newShortcut;
            found = true;
            break;  // Stop iterating once a match is found
        }
    }

    //If we didn't find it to update, let's add it to the end
    if (!found) {
        CHIAKI_LOGI(&log, "Adding Steam entry %s", newShortcut.getExe().toStdString().c_str());
        shortcuts.append(steamTools->buildShortcutEntry(serverNickname, executable, launchOptions, artwork));
    }
    steamTools->updateShortcuts(shortcuts);
    steamTools->updateControllerConfig(newShortcut.getAppName(), controller_layout_workshop_id);
}