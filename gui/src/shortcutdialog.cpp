#include "../include/shortcutdialog.h"

#include <filesystem>
#include <QComboBox>
#include <iostream>
#include <fstream>
#include <qeventloop.h>
#include <QFileDialog>
#include <QLabel>
#include <qtextstream.h>
#include <regex>
#include <sstream>
#include <string>
#include <steamshortcutparser.h>
#include <steamgriddbapi.h>
#include <QMessageBox>

#include "imageloader.h"

ShortcutDialog::ShortcutDialog(Settings *settings, const DisplayServer *server, QWidget* parent) {
    setupUi(this);

    chiaki_log_init(&log, settings->GetLogLevelMask(), chiaki_log_cb_print, this);
    Ui::ShortcutDialog::local_ssid_edit->setText(QString::fromStdString(getConnectedSSID()));

    //Allow External Access Checkbox
    connect(Ui::ShortcutDialog::allow_external_access_checkbox, &QCheckBox::stateChanged, this, &ShortcutDialog::ExternalChanged);

    //Screen Mode combo
    static const QList<QPair<ChiakiScreenModePreset, const char *>> mode_strings = {
        { CHIAKI_MODE_ZOOM, "zoom" },
        { CHIAKI_MODE_STRETCH, "stretch" },
        { CHIAKI_MODE_FULLSCREEN, "fullscreen" }
    };
    for(const auto &p : mode_strings)
    {
        Ui::ShortcutDialog::mode_combo_box->addItem(tr(p.second), p.first);
    }

    //Update the game dropdown for all artworks
    for (auto it = SteamGridDb::gameIDs.begin(); it != SteamGridDb::gameIDs.end(); ++it) {
        Ui::ShortcutDialog::landscape_game_combo->addItem(tr(it->first.c_str()), it->second.c_str());
        Ui::ShortcutDialog::portrait_game_combo->addItem(tr(it->first.c_str()), it->second.c_str());
        Ui::ShortcutDialog::hero_game_combo->addItem(tr(it->first.c_str()), it->second.c_str());
        Ui::ShortcutDialog::icon_game_combo->addItem(tr(it->first.c_str()), it->second.c_str());
        Ui::ShortcutDialog::logo_game_combo->addItem(tr(it->first.c_str()), it->second.c_str());
    }

    //Landscapes
    landscapes = SteamGridDb::getLandscapes(&log, Ui::ShortcutDialog::landscape_game_combo->currentData().toString().toStdString(), 0);
    landscapeIndex = 0;
    loadImage(Ui::ShortcutDialog::landscape_label, landscapes, landscapeIndex);
    connect(Ui::ShortcutDialog::landscape_next_button, &QPushButton::clicked, [=]() {
        RotateImage(RotateDirection::NEXT, Ui::ShortcutDialog::landscape_label, landscapes, landscapeIndex, "landscape");
    });
    connect(Ui::ShortcutDialog::landscape_prev_button, &QPushButton::clicked, [=]() {
        RotateImage(RotateDirection::PREV, Ui::ShortcutDialog::landscape_label, landscapes, landscapeIndex, "landscape");
    });
    connect(Ui::ShortcutDialog::landscape_game_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
        UpdateImageList(ArtworkType::LANDSCAPE, landscapes, Ui::ShortcutDialog::landscape_game_combo->currentData().toString().toStdString(), customLandscape, landscape_prev_button, landscape_next_button);
        if (landscape_game_combo->currentData().toString().toStdString() == "custom") {
            landscapeIndex = 0;
            // Create a QPixmap from the image file
            QPixmap pixmap(QString::fromStdString(customLandscape));
            // Set the pixmap as the image for the QLabel
            landscape_label->setPixmap(pixmap);
        } else {
            loadImage(Ui::ShortcutDialog::landscape_label, landscapes, landscapeIndex);
        }
    });
    connect(Ui::ShortcutDialog::landscape_upload_button, &QPushButton::clicked, [=]() {
        QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"), "", tr("Png Files (*.png)"));
        customLandscape = fileName.toStdString();
        if (landscape_game_combo->count() == SteamGridDb::gameIDs.size()) {
            //Add the custom item
            landscape_game_combo->addItem(tr("Custom"), "custom");
            landscape_game_combo->setCurrentIndex(landscape_game_combo->count()-1);
        }
        landscapes = {customLandscape};
            landscapeIndex = 0;
            // Create a QPixmap from the image file
            QPixmap pixmap(QString::fromStdString(customLandscape));
            // Set the pixmap as the image for the QLabel
            landscape_label->setPixmap(pixmap);
    });

    //Portraits
    portraits = SteamGridDb::getPortraits(&log, Ui::ShortcutDialog::portrait_game_combo->currentData().toString().toStdString(), 0);
    portraitIndex = 0;
    loadImage(Ui::ShortcutDialog::portrait_label, portraits, portraitIndex);
    connect(Ui::ShortcutDialog::portrait_next_button, &QPushButton::clicked, [=]() {
        RotateImage(RotateDirection::NEXT, Ui::ShortcutDialog::portrait_label, portraits, portraitIndex, "portrait");
    });
    connect(Ui::ShortcutDialog::portrait_prev_button, &QPushButton::clicked, [=]() {
        RotateImage(RotateDirection::PREV, Ui::ShortcutDialog::portrait_label, portraits, portraitIndex, "portrait");
    });
    connect(Ui::ShortcutDialog::portrait_game_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
        UpdateImageList(ArtworkType::PORTRAIT, portraits, Ui::ShortcutDialog::portrait_game_combo->currentData().toString().toStdString(), customPortrait, portrait_prev_button, portrait_next_button);
        if (portrait_game_combo->currentData().toString().toStdString() == "custom") {
            portraitIndex = 0;
            // Create a QPixmap from the image file
            QPixmap pixmap(QString::fromStdString(customPortrait));
            // Set the pixmap as the image for the QLabel
            portrait_label->setPixmap(pixmap);
        } else {
            loadImage(Ui::ShortcutDialog::portrait_label, portraits, portraitIndex);
        }
    });
    connect(Ui::ShortcutDialog::portrait_upload_button, &QPushButton::clicked, [=]() {
        QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"), "", tr("Png Files (*.png)"));
        customPortrait = fileName.toStdString();
        if (portrait_game_combo->count() == SteamGridDb::gameIDs.size()) {
            //Add the custom item
            portrait_game_combo->addItem(tr("Custom"), "custom");
            portrait_game_combo->setCurrentIndex(portrait_game_combo->count()-1);
        }
        portraits = {customPortrait};
            portraitIndex = 0;
            // Create a QPixmap from the image file
            QPixmap pixmap(QString::fromStdString(customPortrait));
            // Set the pixmap as the image for the QLabel
            portrait_label->setPixmap(pixmap);
    });

    //Heroes
    heroes = SteamGridDb::getHeroes(&log, Ui::ShortcutDialog::hero_game_combo->currentData().toString().toStdString(), 0);
    heroIndex = 0;
    loadImage(Ui::ShortcutDialog::hero_label, heroes, heroIndex);
    connect(Ui::ShortcutDialog::hero_next_button, &QPushButton::clicked, [=]() {
        RotateImage(RotateDirection::NEXT, Ui::ShortcutDialog::hero_label, heroes, heroIndex, "hero");
    });
    connect(Ui::ShortcutDialog::hero_prev_button, &QPushButton::clicked, [=]() {
        RotateImage(RotateDirection::PREV, Ui::ShortcutDialog::hero_label, heroes, heroIndex, "hero");
    });
    connect(Ui::ShortcutDialog::hero_game_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
        UpdateImageList(ArtworkType::HERO, heroes, Ui::ShortcutDialog::hero_game_combo->currentData().toString().toStdString(), customHero, hero_prev_button, hero_next_button);
        if (hero_game_combo->currentData().toString().toStdString() == "custom") {
            heroIndex = 0;
            // Create a QPixmap from the image file
            QPixmap pixmap(QString::fromStdString(customHero));
            // Set the pixmap as the image for the QLabel
            hero_label->setPixmap(pixmap);
        } else {
            loadImage(Ui::ShortcutDialog::hero_label, heroes, heroIndex);
        }
    });
    connect(Ui::ShortcutDialog::hero_upload_button, &QPushButton::clicked, [=]() {
        QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"), "", tr("Png Files (*.png)"));
        customHero = fileName.toStdString();
        if (hero_game_combo->count() == SteamGridDb::gameIDs.size()) {
            //Add the custom item
            hero_game_combo->addItem(tr("Custom"), "custom");
            hero_game_combo->setCurrentIndex(hero_game_combo->count()-1);
        }
        heroes = {customHero};
            heroIndex = 0;
            // Create a QPixmap from the image file
            QPixmap pixmap(QString::fromStdString(customHero));
            // Set the pixmap as the image for the QLabel
            hero_label->setPixmap(pixmap);
    });

    //Icons
    icons = SteamGridDb::getIcons(&log, Ui::ShortcutDialog::icon_game_combo->currentData().toString().toStdString(), 0);
    iconIndex = 0;
    loadImage(Ui::ShortcutDialog::icon_label, icons, iconIndex);
    connect(Ui::ShortcutDialog::icon_next_button, &QPushButton::clicked, [=]() {
        RotateImage(RotateDirection::NEXT, Ui::ShortcutDialog::icon_label, icons, iconIndex, "icon");
    });
    connect(Ui::ShortcutDialog::icon_prev_button, &QPushButton::clicked, [=]() {
        RotateImage(RotateDirection::PREV, Ui::ShortcutDialog::icon_label, icons, iconIndex, "icon");
    });
    connect(Ui::ShortcutDialog::icon_game_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
        UpdateImageList(ArtworkType::ICON, icons, Ui::ShortcutDialog::icon_game_combo->currentData().toString().toStdString(), customIcon, icon_prev_button, icon_next_button);
        if (icon_game_combo->currentData().toString().toStdString() == "custom") {
            iconIndex = 0;
            // Create a QPixmap from the image file
            QPixmap pixmap(QString::fromStdString(customIcon));
            // Set the pixmap as the image for the QLabel
            icon_label->setPixmap(pixmap);
        } else {
            loadImage(Ui::ShortcutDialog::icon_label, icons, iconIndex);
        }
    });
    connect(Ui::ShortcutDialog::icon_upload_button, &QPushButton::clicked, [=]() {
        QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"), "", tr("Png Files (*.png)"));
        customIcon = fileName.toStdString();
        if (icon_game_combo->count() == SteamGridDb::gameIDs.size()) {
            //Add the custom item
            icon_game_combo->addItem(tr("Custom"), "custom");
            icon_game_combo->setCurrentIndex(icon_game_combo->count()-1);
        }
        icons = {customIcon};
            iconIndex = 0;
            // Create a QPixmap from the image file
            QPixmap pixmap(QString::fromStdString(customIcon));
            // Set the pixmap as the image for the QLabel
            icon_label->setPixmap(pixmap);
    });

    //Logos
    logos = SteamGridDb::getLogos(&log, Ui::ShortcutDialog::logo_game_combo->currentData().toString().toStdString(), 0);
    logoIndex = 0;
    loadImage(Ui::ShortcutDialog::logo_label, logos, logoIndex);
    connect(Ui::ShortcutDialog::logo_next_button, &QPushButton::clicked, [=]() {
        RotateImage(RotateDirection::NEXT, Ui::ShortcutDialog::logo_label, logos, logoIndex, "logo");
    });
    connect(Ui::ShortcutDialog::logo_prev_button, &QPushButton::clicked, [=]() {
        RotateImage(RotateDirection::PREV, Ui::ShortcutDialog::logo_label, logos, logoIndex, "logo");
    });
    connect(Ui::ShortcutDialog::logo_game_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
        UpdateImageList(ArtworkType::LOGO, logos, Ui::ShortcutDialog::logo_game_combo->currentData().toString().toStdString(), customLogo, logo_prev_button, logo_next_button);
        if (logo_game_combo->currentData().toString().toStdString() == "custom") {
            logoIndex = 0;
            // Create a QPixmap from the image file
            QPixmap pixmap(QString::fromStdString(customLogo));
            // Set the pixmap as the image for the QLabel
            logo_label->setPixmap(pixmap);
        } else {
            loadImage(Ui::ShortcutDialog::logo_label, logos, logoIndex);
        }
    });
    connect(Ui::ShortcutDialog::logo_upload_button, &QPushButton::clicked, [=]() {
        QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"), "", tr("Png Files (*.png)"));
        customLogo = fileName.toStdString();
        if (logo_game_combo->count() == SteamGridDb::gameIDs.size()) {
            //Add the custom item
            logo_game_combo->addItem(tr("Custom"), "custom");
            logo_game_combo->setCurrentIndex(logo_game_combo->count()-1);
        }
        logos = {customLogo};
            logoIndex = 0;
            // Create a QPixmap from the image file
            QPixmap pixmap(QString::fromStdString(customLogo));
            // Set the pixmap as the image for the QLabel
            logo_label->setPixmap(pixmap);
    });

    //Shortcut Button
    connect(Ui::ShortcutDialog::add_to_steam_button, &QPushButton::clicked, [=]() {
        std::map<std::string, std::string> artwork;
        artwork["landscape"] = landscapes.at(landscapeIndex);
        artwork["portrait"] = portraits.at(portraitIndex);
        artwork["hero"] = heroes.at(heroIndex);
        artwork["logo"] = logos.at(logoIndex);
        artwork["icon"] = icons.at(iconIndex);
        CreateShortcut(server, artwork);
    });
}

void ShortcutDialog::loadImage(QLabel* label, std::vector<std::string> images, int index) {
    label->clear();
    label->setText("Loading");
    std::string url = images.at(index);
    ImageLoader imageLoader(label);
    imageLoader.loadImage(QString::fromStdString(url));
    label->setText("");
}

void ShortcutDialog::UpdateImageList(ArtworkType artworktype, std::vector<std::string>& images, std::string gameId, std::string& custom, QPushButton* prev, QPushButton* next) {
    if (gameId == "custom") {
        images = {custom};
        prev->setEnabled(false);
        next->setEnabled(false);
    } else {
        prev->setEnabled(true);
        next->setEnabled(true);
        switch (artworktype) {
            case ArtworkType::LANDSCAPE:
                images = SteamGridDb::getLandscapes(&log, gameId, 0);
                landscapeIndex = 0;
                break;
            case ArtworkType::PORTRAIT:
                images = SteamGridDb::getPortraits(&log, gameId, 0);
                portraitIndex = 0;
                break;
            case ArtworkType::HERO:
                images = SteamGridDb::getHeroes(&log, gameId, 0);
                heroIndex = 0;
                break;
            case ArtworkType::ICON:
                images = SteamGridDb::getIcons(&log, gameId, 0);
                iconIndex = 0;
                break;
            case ArtworkType::LOGO:
                images = SteamGridDb::getLogos(&log, gameId, 0);
                logoIndex = 0;
                break;
        }
    }
}

void ShortcutDialog::RotateImage(RotateDirection direction, QLabel* label, std::vector<std::string>& images, int& index, std::string type) {
    if (direction == RotateDirection::NEXT) {
        index++;
        //If we've run out of images
        if (index >= images.size()) {
            index = 0;
        }
    } else {
        index--;
        //Loop around the bottom
        if (index < 0) {
            index = (images.size() - 1);
        }
    }
    loadImage(label, images, index);
}

void ShortcutDialog::ExternalChanged() {
    Ui::ShortcutDialog::external_dns_edit->setFocus();
    Ui::ShortcutDialog::external_dns_edit->setEnabled(Ui::ShortcutDialog::allow_external_access_checkbox->isChecked());
    Ui::ShortcutDialog::local_ssid_edit->setEnabled(Ui::ShortcutDialog::allow_external_access_checkbox->isChecked());
}

void ShortcutDialog::CreateShortcut(const DisplayServer* displayServer, std::map<std::string, std::string> artwork) {
    //Create a map of variables for the template
    std::map<std::string, std::string> paramMap;

    paramMap["home_ssid"] = local_ssid_edit->text().toStdString();
    paramMap["ps_console"] = displayServer->IsPS5() ? "5" : "4";
    paramMap["wait_timeout"] = "35";
    paramMap["home_addr"] = displayServer->GetHostAddr().toStdString();
    paramMap["away_addr"] = external_dns_edit->text().toStdString();
    paramMap["regist_key"] = displayServer->registered_host.GetRPRegistKey().toStdString();
    paramMap["login_passcode"] = passcode_edit->text().toStdString();
    paramMap["mode"] = mode_combo_box->currentText().toStdString();
    paramMap["server_nickname"] = displayServer->discovered ? displayServer->discovery_host.host_name.toStdString() : displayServer->registered_host.GetServerNickname().toStdString();

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

    std::string filePath;
    if (steamExists) {
        filePath.append(getenv("HOME"));
        filePath.append("/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/");
        filePath.append(paramMap["server_nickname"]);
        filePath.append(".sh");
        SteamShortcutParser::createDirectories(&log, filePath);
    } else {
        int createShortcut = QMessageBox::critical(nullptr, "Steam not found", QString::fromStdString("Steam not found! Save shortcut elsewhere?"), QMessageBox::Ok, QMessageBox::Cancel);
        if (createShortcut == QMessageBox::Cancel) {
            return;
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
    const std::string chmodCommand = "chmod +x " + filePath;

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
        std::cerr << "Error executing command." << std::endl;
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