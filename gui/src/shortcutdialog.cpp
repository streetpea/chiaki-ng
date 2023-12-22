#include "../include/shortcutdialog.h"

#include <filesystem>
#include <QComboBox>
#include <fstream>
#include <qeventloop.h>
#include <QFileDialog>
#include <qtextstream.h>
#include <regex>
#include <sstream>
#include <array>
#include <string>
#include <steamshortcutparser.h>
#include <QMessageBox>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "imageloader.h"
#include "steamgriddbapi.h"

ShortcutDialog::ShortcutDialog(Settings *settings, const DisplayServer *server, QWidget* parent) {
    setupUi(this);

    chiaki_log_init(&log, settings->GetLogLevelMask(), chiaki_log_cb_print, this);
    Ui::ShortcutDialog::local_ssid_edit->setText(QString::fromStdString(getConnectedSSID()));

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
        Ui::ShortcutDialog::mode_combo_box->addItem(tr(p.second), p.first);
    }

    //Widgets
    artworkWidgets.insert(ArtworkType::LANDSCAPE,
        new SGDBArtworkWidget(this, &log, ArtworkType::LANDSCAPE, landscape_label, landscape_upload_button, landscape_prev_button, landscape_next_button, landscape_game_combo));
    artworkWidgets.insert(ArtworkType::PORTRAIT,
        new SGDBArtworkWidget(this, &log, ArtworkType::PORTRAIT, portrait_label, portrait_upload_button, portrait_prev_button, portrait_next_button, portrait_game_combo));
    artworkWidgets.insert(ArtworkType::HERO,
        new SGDBArtworkWidget(this, &log, ArtworkType::HERO, hero_label, hero_upload_button, hero_prev_button, hero_next_button, hero_game_combo));
    artworkWidgets.insert(ArtworkType::ICON,
        new SGDBArtworkWidget(this, &log, ArtworkType::ICON, icon_label, icon_upload_button, icon_prev_button, icon_next_button, icon_game_combo));
    artworkWidgets.insert(ArtworkType::LOGO,
        new SGDBArtworkWidget(this, &log, ArtworkType::LOGO, logo_label, logo_upload_button, logo_prev_button, logo_next_button, logo_game_combo));

    //Shortcut Button
    connect(Ui::ShortcutDialog::add_to_steam_button, &QPushButton::clicked, [=]() {
        std::map<std::string, std::string> artwork;
        artwork["landscape"] = artworkWidgets.value(ArtworkType::LANDSCAPE)->getUrl().toStdString();
        artwork["portrait"] = artworkWidgets.value(ArtworkType::PORTRAIT)->getUrl().toStdString();
        artwork["hero"] = artworkWidgets.value(ArtworkType::HERO)->getUrl().toStdString();
        artwork["logo"] = artworkWidgets.value(ArtworkType::LOGO)->getUrl().toStdString();
        artwork["icon"] = artworkWidgets.value(ArtworkType::ICON)->getUrl().toStdString();
        CreateShortcut(server, artwork);
    });

    //QTimer::singleShot(0, this, &ShortcutDialog::dialogLoaded);
}

void ShortcutDialog::ExternalChanged() {
    Ui::ShortcutDialog::external_dns_edit->setFocus();
    Ui::ShortcutDialog::external_dns_edit->setEnabled(Ui::ShortcutDialog::allow_external_access_checkbox->isChecked());
    Ui::ShortcutDialog::local_ssid_edit->setEnabled(Ui::ShortcutDialog::allow_external_access_checkbox->isChecked());
}

std::string ShortcutDialog::getExecutable() {
#if defined(__APPLE__)
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);

    if (_NSGetExecutablePath(buffer, &size) == -1) {
        // Buffer is not large enough, allocate a larger one
        char* largerBuffer = new char[size];
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
            return buffer;
        }
    }
#endif
    return "";
}

void ShortcutDialog::CreateShortcut(const DisplayServer* displayServer, std::map<std::string, std::string> artwork) {
    //Create a map of variables for the template
    std::map<std::string, std::string> paramMap;

    paramMap["home_ssid"] = local_ssid_edit->text().toStdString();
    paramMap["ps_console"] = displayServer->IsPS5() ? "5" : "4";
    paramMap["wait_timeout"] = "35";
    paramMap["home_addr"] = displayServer->GetHostAddr().toStdString();
    paramMap["away_addr"] = external_dns_edit->text().toStdString();
    paramMap["regist_key"] = QString::fromUtf8(displayServer->registered_host.GetRPRegistKey()).toStdString();
    paramMap["login_passcode"] = passcode_edit->text().toStdString();
    paramMap["mode"] = mode_combo_box->currentText().toStdString();
    paramMap["server_nickname"] = displayServer->discovered ? displayServer->discovery_host.host_name.toStdString() : displayServer->registered_host.GetServerNickname().toStdString();
    paramMap["executable"] = getExecutable();
    std::stringstream escapeCommandStream;
    escapeCommandStream << "printf %q " << "\"" << paramMap["server_nickname"] << "\"";
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


    std::string fileText = compileTemplate("shortcut.tmpl", paramMap);

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

    bool steamExists = SteamShortcutParser::steamExists();
    std::string executable = getExecutable();

    std::string filePath;
    if (steamExists && executable.rfind("flatpak run io.github.streetpea.Chiaki4deck", 0) == 0) {
        filePath.append(getenv("HOME"));
        filePath.append("/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/");
        filePath.append(paramMap["server_nickname"]);
        filePath.append(".sh");
        SteamShortcutParser::createDirectories(&log, filePath);
    } else {
        if (!steamExists) {
            int createShortcut = QMessageBox::critical(nullptr, "Steam not found", QString::fromStdString("Steam not found! Save shortcut elsewhere?"), QMessageBox::Ok, QMessageBox::Cancel);
            if (createShortcut == QMessageBox::Cancel) {
                return;
            }
        }
        filePath.append(QFileDialog::getSaveFileName(nullptr, "Save Shell Script", QDir::homePath() + QDir::separator() + paramMap["server_nickname"].c_str() + ".sh", "Shell Scripts (*.sh)").toStdString());
    }

    // Check if the user canceled the dialog
    if (QString::fromStdString(filePath).isEmpty()) {
        return;
    }

    // Open the file for writing
    QFile outputFile(QString::fromStdString(filePath));
    if (outputFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        // Write the string to the file
        QTextStream out(&outputFile);
        out << QString::fromStdString(fileText);

        // Close the file
        outputFile.close();
    }

    // Construct the shell command to make the file executable
    std::stringstream chmodCommandStream;
    chmodCommandStream << "chmod +x " << "\"" << filePath << "\"";
    const std::string chmodCommand = chmodCommandStream.str();


    // Execute the shell command to make it executable
    std::system(chmodCommand.c_str());

    std::string steamBaseDir = SteamShortcutParser::getSteamBaseDir();
    if (steamExists) {
        AddToSteam(displayServer, filePath, artwork);
        QMessageBox::information(nullptr, "Success", QString::fromStdString("Added "+paramMap["server_nickname"]+" to Steam. Please restart Steam."), QMessageBox::Ok);
    } else {
        QMessageBox::information(nullptr, "Success", QString::fromStdString("Saved shortcut to "+paramMap["server_nickname"]), QMessageBox::Ok);
    }
    close();
}

std::string ShortcutDialog::compileTemplate(const std::string& templateFile, const std::map<std::string, std::string>& inputMap) {
    // Specify the resource path with the prefix
    QString resourcePath = QString::fromStdString(":/templates/"+templateFile);

    // Open the resource file
    QFile file(resourcePath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return file.errorString().toStdString();
    }

    // Read the contents of the file into a QString
    QTextStream in(&file);
    QString fileContents = in.readAll();

    // Close the file
    file.close();

    const std::regex variablePattern(R"(\{\{([A-Za-z_]+)\}\})");

    std::string result = fileContents.toStdString();
    std::smatch match;

    while (std::regex_search(result, match, variablePattern)) {
        if (match.size() == 2) {
            const std::string& variableName = match[1].str();
            auto it = inputMap.find(variableName);

            if (it != inputMap.end()) {
                result.replace(match.position(), match.length(), it->second);
            }
        }
    }

    return result+"\n";
}

std::string ShortcutDialog::getConnectedSSID() {
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
    std::string result = "";

    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != nullptr)
            result += buffer;
    }

    pclose(pipe);

    // Remove newline characters
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());

    return result;
}

void ShortcutDialog::AddToSteam(const DisplayServer* server, std::string filePath, std::map<std::string, std::string> artwork) {
    std::vector<std::map<std::string, std::string>> shortcuts = SteamShortcutParser::parseShortcuts(&log);

    std::map<std::string, std::string> newShortcut = SteamShortcutParser::buildShortcutEntry(&log, server, filePath, artwork);

    bool found = false;
    //Look to see if we need to update
    for (auto& map : shortcuts) {
        // Check if the key exists and its value matches the valueToSearch
        auto it = map.find("Exe");
        if (it != map.end() && it->second == SteamShortcutParser::getValueFromMap(newShortcut, "Exe")) {
            // Replace the entire map with the new one

            CHIAKI_LOGI(&log, "Updating Steam entry");
            map = newShortcut;
            found = true;
            break;  // Stop iterating once a match is found
        }
    }

    //If we didn't find it to update, let's add it to the end
    if (!found) {
        CHIAKI_LOGI(&log, "Adding Steam entry");
        shortcuts.emplace_back(SteamShortcutParser::buildShortcutEntry(&log, server, filePath, artwork));
    }
    SteamShortcutParser::updateShortcuts(&log, shortcuts);
    SteamShortcutParser::updateControllerConfig(&log, newShortcut["AppName"]);
}