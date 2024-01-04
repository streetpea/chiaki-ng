#include "../include/shortcutdialog.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <QComboBox>
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

#include <QStandardPaths>

#include "imageloader.h"

#ifdef CHIAKI_ENABLE_ARTWORK
#include "artworkdialog.h"
#endif

QString controller_layout_workshop_id = "3049833406";

ShortcutDialog::ShortcutDialog(Settings *settings, const DisplayServer *server, QWidget* parent) {
    setupUi(this);
    chiaki_log_init(&log, settings->GetLogLevelMask(), chiaki_log_cb_print, this);

    //Create steam tools
    auto infoLambda = [this](const QString &infoMessage) {
        CHIAKI_LOGI(&log, infoMessage.toStdString().c_str());
    };

    auto errorLambda = [this](const QString &errorMessage) {
        CHIAKI_LOGE(&log, errorMessage.toStdString().c_str());
    };
    steamTools = new SteamTools(infoLambda, errorLambda);

    //Prepopulate the SSID edit box
    local_ssid_edit->setText(getConnectedSSID());

    //Allow External Access Checkbox
    connect(Ui::ShortcutDialog::allow_external_access_checkbox, &QCheckBox::stateChanged, this, &ShortcutDialog::ExternalChanged);

    //Screen Mode combo
    static const QList<QPair<ChiakiScreenModePreset, const char *>> mode_strings = {
        { CHIAKI_MODE_STRETCH, "stretch" },
        { CHIAKI_MODE_ZOOM, "zoom" },
        { CHIAKI_MODE_FULLSCREEN, "fullscreen" }
    };
    for(const auto &p : mode_strings)
    {
        mode_combo_box->addItem(tr(p.second), p.first);
    }

    #ifdef CHIAKI_ENABLE_ARTWORK
        //Artwork
        choose_artwork_label->setVisible(true);
        choose_artwork_button->setVisible(true);
        connect(choose_artwork_button, &QPushButton::clicked, [this]() {
            auto dialog = new ArtworkDialog(this);
            connect(dialog, &ArtworkDialog::updateArtwork, this, &ShortcutDialog::updateArtwork);
            dialog->exec();
        });
    #endif

    //Shortcut Button
    connect(Ui::ShortcutDialog::add_to_steam_button, &QPushButton::clicked, [=]() {
        CreateShortcut(server);
    });
}

/**
 * \brief Enable ot disable the SSID and DNS entries when external access is ticked/unticked
 */
void ShortcutDialog::ExternalChanged() {
    Ui::ShortcutDialog::external_dns_edit->setFocus();
    Ui::ShortcutDialog::external_dns_edit->setEnabled(Ui::ShortcutDialog::allow_external_access_checkbox->isChecked());
    Ui::ShortcutDialog::local_ssid_edit->setEnabled(Ui::ShortcutDialog::allow_external_access_checkbox->isChecked());
}

/**
 * \brief Get the path to the chiaki4deck executable for use in the automation script in the form of a QString.
 * For Mac this uses _NSGetExecutablePath
 * For Linux it checks if installed via flatpak and uses `flatpak run`
 * if not, it uses readlink to check /proc/self/exe
 *
 * \return QString of the executable
 */
QString ShortcutDialog::getExecutable() {
#if defined(__APPLE__)
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);

    if (_NSGetExecutablePath(buffer, &size) == -1) {
        // Buffer is not large enough, allocate a larger one
        auto largerBuffer = new char[size];
        if (_NSGetExecutablePath(largerBuffer, &size) == 0) {
            return largerBuffer;
        }
    } else {
        return buffer;
    }
#elif defined(__linux)
    std::string chiakiFlatpakDir = "";

    chiakiFlatpakDir.append(getenv("HOME"));
    chiakiFlatpakDir.append("/.var/app/io.github.streetpea.Chiaki4deck");
    //Installed via Flatpak
    if (access(chiakiFlatpakDir.c_str(), 0) == 0) {
        return "flatpak run io.github.streetpea.Chiaki4deck";
    }
    else {
        char buffer[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);

        if (len != -1) {
            buffer[len] = '\0';
            return std::string(buffer);
        }
    }
#endif
    return "";
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
    //Get the Chiaki4Deck executable
    QString executable = getExecutable();

    //Create a map of variables for the template
    QMap<QString, QString> paramMap;

    paramMap.insert("home_ssid", local_ssid_edit->text()); //Entered by the user
    paramMap.insert("ps_console", displayServer->IsPS5() ? "5" : "4"); //Determined from the DisplayServer object
    paramMap.insert("wait_timeout", "35"); //Always 35
    paramMap.insert("home_addr", displayServer->GetHostAddr()); //Determined from the DisplayServer object
    paramMap.insert("away_addr", external_dns_edit->text()); //Entered by the User
    paramMap.insert("regist_key", QString::fromUtf8(displayServer->registered_host.GetRPRegistKey())); //Determined from the DisplayServer object
    paramMap.insert("login_passcode", passcode_edit->text()); //Entered by the User
    paramMap.insert("mode", mode_combo_box->currentText()); //Selected by the User
    paramMap.insert("server_nickname", displayServer->discovered ? displayServer->discovery_host.host_name : displayServer->registered_host.GetServerNickname()); //Determined from the DisplayServer object
    paramMap.insert("executable", executable); //Fetched from ::getExecutable()

    //Escape server nickname for script
    std::stringstream escapeCommandStream;
    escapeCommandStream << "printf %q " << "\"" << paramMap.value("server_nickname").toStdString() << "\"";
    const std::string escapeCommandStr = escapeCommandStream.str();
    const char* escapeCommand = escapeCommandStr.c_str();
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(escapeCommand, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        paramMap["escaped_server_nickname"] += buffer.data();
    }


    //Compile the shell script
    QString fileText = compileTemplate("shortcut.tmpl", paramMap);
    if (allow_external_access_checkbox->isChecked()) {
        if (displayServer->IsPS5()) {
            fileText = fileText+compileTemplate("ps5_external.tmpl", paramMap);
            fileText = fileText+compileTemplate("discover_wakeup.tmpl", paramMap);
        } else {
            fileText = fileText+compileTemplate("ps4_external.tmpl", paramMap);
        }
    } else {
        fileText = fileText+compileTemplate("local.tmpl", paramMap);
        fileText = fileText+compileTemplate("discover_wakeup.tmpl", paramMap);
    }
    fileText = fileText+compileTemplate("launch.tmpl", paramMap);

    bool steamExists = steamTools->steamExists();

    //If Steam exists, set the output file to AppConfigLocation, if not, ask the user where to save.
    QString filePath;
    if (steamExists) {
        filePath = QString("%1/%2.sh")
            .arg(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .arg(paramMap.value("server_nickname"));
    } else {
        int createShortcut = QMessageBox::critical(nullptr, "Steam not found", QString::fromStdString("Steam not found! Save shortcut elsewhere?"), QMessageBox::Ok, QMessageBox::Cancel);
        if (createShortcut == QMessageBox::Cancel) {
            return;
        }
        filePath.append(QFileDialog::getSaveFileName(nullptr, "Save Shell Script", QDir::homePath() + QDir::separator() + paramMap.value("server_nickname") + ".sh", "Shell Scripts (*.sh)"));
        // Check if the user canceled the dialog
        if (filePath.isEmpty()) {
            return;
        }
    }

    // Open the file for writing
    QFile outputFile(filePath);
    if (outputFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        // Write the string to the file
        QTextStream out(&outputFile);
        out << fileText;

        //Set permissions
        outputFile.setPermissions(QFile::ExeOwner);

        // Close the file
        outputFile.close();
    }

    if (steamExists) {
        AddToSteam(displayServer, filePath);
        QMessageBox::information(nullptr, "Success", QString("Added %1 to Steam. Please restart Steam.").arg(paramMap.value("server_nickname")), QMessageBox::Ok);
    } else {
        QMessageBox::information(nullptr, "Success", QString("Saved shortcut to %1").arg(paramMap.value("server_nickname")), QMessageBox::Ok);
    }
    close();
}

/**
 *
 * \brief Reads the contents of a specified template file, which contains
 * placeholders in the form of "{{variableName}}". It then replaces these placeholders
 * with corresponding values from the provided input map. The compiled template is
 * returned as a QString.
 *
 * \param templateFile The name of the template file to be compiled.
 * \param inputMap A QMap containing key-value pairs where keys represent variable names
 *                 and values are the replacements for the corresponding placeholders.
 *
 * \return The compiled template as a QString. If an error occurs while opening the
 *         template file, the error message is returned.
 */
QString ShortcutDialog::compileTemplate(const QString& templateFile, const QMap<QString, QString>& inputMap) {
    // Specify the resource path with the prefix
    QString resourcePath = QString(":/templates/%1").arg(templateFile);

    // Open the resource file
    QFile file(resourcePath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return file.errorString();
    }

    // Read the contents of the file into a QString
    QTextStream in(&file);
    QString fileContents = in.readAll();

    // Close the file
    file.close();

    QRegularExpression pattern(R"(\{\{([A-Za-z_]+)\}\})");

    QString result = fileContents;
    QRegularExpressionMatchIterator matchIterator = pattern.globalMatch(result);

    //Loop through the string finding the first {variableName} and replacing it, keep going until there are no more matches
    while (matchIterator.hasNext()) {
        QRegularExpressionMatch match = matchIterator.next();

        if (match.capturedTexts().size() == 2) {
            const QString &variableName = match.captured(1);
            auto it = inputMap.find(variableName);

            if (it != inputMap.end()) {
                result.replace(match.capturedStart(), match.capturedLength(), it.value());
            }
        }
    }

    return result+"\n";
}

/**
 * \brief Use system commands for each OS to get the current SSID for the first wlan as a QString
 * If unknown OS is detected throw an error
 *
 * \return QString of the connected SSID
 */
QString ShortcutDialog::getConnectedSSID() {
    std::string command;
    #if defined(__APPLE__)
        command = "/System/Library/PrivateFrameworks/Apple80211.framework/Resources/airport -I | grep SSID | grep -v BSSID | awk -F: '{print $2}'";
    #elif defined(__linux__)
        // Linux and macOS
        command = "iwgetid -r";
    #elif defined(_WIN32)
        // Windows
        command = "netsh wlan show interfaces | findstr /R \"^\\s*SSID\"";
    #else
        #error "Unsupported platform"
    #endif

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        CHIAKI_LOGE(&log, "Error executing command.");
        return "";
    }

    char buffer[128];
    QString result = "";

    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != nullptr)
            result += buffer;
    }

    pclose(pipe);

    // Remove newline characters
    result.remove(QChar('\n'));
    result.remove(QChar('\r'));

    return result;
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
void ShortcutDialog::AddToSteam(const DisplayServer* server, QString filePath) {
    //Get the current shortcuts
    QVector<SteamShortcutEntry> shortcuts = steamTools->parseShortcuts();

    QString serverNickname = server->registered_host.GetServerNickname();

    //Create a new shortcut entry
    SteamShortcutEntry newShortcut = steamTools->buildShortcutEntry(serverNickname, filePath, artwork);

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
        shortcuts.append(steamTools->buildShortcutEntry(serverNickname, filePath, artwork));
    }
    steamTools->updateShortcuts(shortcuts);
    steamTools->updateControllerConfig(newShortcut.getAppName(), controller_layout_workshop_id);
}