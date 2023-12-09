//
// Created by Jamie Bartlett on 04/12/2023.
//

#include "../include/shortcutdialog.h"

#include <filesystem>
#include <QComboBox>
#include <QFormLayout>
#include <QPushButton>
#include <iostream>
#include <fstream>
#include <qeventloop.h>
#include <QFileDialog>
#include <QGroupBox>
#include <QLabel>
#include <qtextstream.h>
#include <regex>
#include <sstream>
#include <string>
#include <vdfparser.h>
#include <steamgriddbapi.h>
#include <QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QMessageBox>

#include "imageloader.h"


ShortcutDialog::ShortcutDialog(const DisplayServer *server, QWidget* parent) {
    this->server = server;
    const std::string windowTitle{"Add to Steam "+(server->discovered ? server->discovery_host.host_name.toStdString() : server->registered_host.GetServerNickname().toStdString())};
    setWindowTitle(tr(windowTitle.c_str()));

    auto root_layout = new QVBoxLayout(this);
    setLayout(root_layout);

    auto vertical_layout = new QVBoxLayout();
    root_layout->addLayout(vertical_layout);

    // Settings
    auto settings_group_box = new QGroupBox(tr("Shortcut Settings"));
    vertical_layout->addWidget(settings_group_box);

    auto settings_layout = new QFormLayout();
    settings_group_box->setLayout(settings_layout);
    if(settings_layout->spacing() < 16)
        settings_layout->setSpacing(16);

    allow_external_access_checkbox = new QCheckBox(this);
    settings_layout->addRow(tr("Access this Playstation externally?"), allow_external_access_checkbox);
    connect(allow_external_access_checkbox, &QCheckBox::stateChanged, this, &ShortcutDialog::ExternalChanged);

    external_group_box = new QGroupBox(tr("External Access"));
    settings_layout->addRow(external_group_box);

    auto external_layout = new QFormLayout();
    external_group_box->setLayout(external_layout);
    if(external_layout->spacing() < 16)
        external_layout->setSpacing(16);

    external_group_box->setVisible(false);

    external_dns_edit = new QLineEdit(this);
    external_layout->addRow(tr("External IP Address or DNS Entry"), external_dns_edit);

    local_ssid_edit = new QLineEdit(this);
    external_layout->addRow(tr("What's the SSID of your home wifi?"), local_ssid_edit);
    local_ssid_edit->setText(QString::fromStdString(getConnectedSSID()));

    mode_combo_box = new QComboBox(this);
    static const QList<QPair<ChiakiScreenModePreset, const char *>> mode_strings = {
        { CHIAKI_MODE_ZOOM, "zoom" },
        { CHIAKI_MODE_STRETCH, "stretch" },
        { CHIAKI_MODE_FULLSCREEN, "fullscreen" }
    };
    for(const auto &p : mode_strings)
    {
        mode_combo_box->addItem(tr(p.second), p.first);
    }
    settings_layout->addRow(tr("Mode: "), mode_combo_box);

    passcode_edit = new QLineEdit(this);
    settings_layout->addRow(tr("4 Digit passcode (optional):"), passcode_edit);

    //Artwork
    auto grid_group_box = new QGroupBox(tr("Grid Artwork"));
    vertical_layout->addWidget(grid_group_box);

    auto grid_layout = new QVBoxLayout();
    grid_group_box->setLayout(grid_layout);
    if(grid_layout->spacing() < 16)
        grid_layout->setSpacing(16);

    auto art_line_one = new QHBoxLayout();
    grid_layout->addLayout(art_line_one);

    auto art_line_two = new QHBoxLayout();
    grid_layout->addLayout(art_line_two);

    //Landscapes
    auto landscapeLayout = new QVBoxLayout();
    art_line_one->addLayout(landscapeLayout);
    landscapes = SteamGridDb::getLandscapes(0);
    landscapeIndex = 0;
    landscapeLabel = new QLabel(this);
    landscapeLabel->setScaledContents(true);
    landscapeLabel->setFixedWidth(200);
    landscapeLabel->setFixedHeight(94);
    landscapeLayout->addWidget(landscapeLabel);
    loadImage(landscapeLabel, landscapes, landscapeIndex);
    auto nextLandscapeButton = new QPushButton(tr("Next Landscape"), this);
    nextLandscapeButton->setFixedWidth(120);
    landscapeLayout->addWidget(nextLandscapeButton);
    connect(nextLandscapeButton, &QPushButton::clicked, [=]() {
        RotateImage(landscapeLabel, landscapes, landscapeIndex, "landscape");
    });

    //Portraits
    auto portraitLayout = new QVBoxLayout();
    art_line_one->addLayout(portraitLayout);
    portraits = SteamGridDb::getPortraits(0);
    portraitIndex = 0;
    portraitLabel = new QLabel(this);
    portraitLabel->setScaledContents(true);
    portraitLabel->setFixedWidth(132);
    portraitLabel->setFixedHeight(200);
    portraitLayout->addWidget(portraitLabel);
    loadImage(portraitLabel, portraits, portraitIndex);
    auto nextPortraitButton = new QPushButton(tr("Next Portrait"), this);
    nextPortraitButton->setFixedWidth(110);
    portraitLayout->addWidget(nextPortraitButton);
    connect(nextPortraitButton, &QPushButton::clicked, [=]() {
        RotateImage(portraitLabel, portraits, portraitIndex, "portrait");
    });

    //Heroes
    auto heroLayout = new QVBoxLayout();
    art_line_two->addLayout(heroLayout);
    heroes = SteamGridDb::getHeroes(0);
    heroIndex = 0;
    heroLabel = new QLabel(this);
    heroLabel->setScaledContents(true);
    heroLabel->setFixedWidth(200);
    heroLabel->setFixedHeight(94);
    heroLayout->addWidget(heroLabel);
    loadImage(heroLabel, heroes, heroIndex);
    auto nextHeroButton = new QPushButton(tr("Next Hero"), this);
    heroLayout->addWidget(nextHeroButton);
    connect(nextHeroButton, &QPushButton::clicked, [=]() {
        RotateImage(heroLabel, heroes, heroIndex, "hero");
    });

    //Logos
    auto logoLayout = new QVBoxLayout();
    art_line_two->addLayout(logoLayout);
    logos = SteamGridDb::getLogos(0);
    logoIndex = 0;
    logoLabel = new QLabel(this);
    logoLabel->setScaledContents(true);
    logoLabel->setMaximumWidth(200);
    logoLabel->setMaximumHeight(94);
    logoLayout->addWidget(logoLabel);
    loadImage(logoLabel, logos, logoIndex);
    auto nextLogoButton = new QPushButton(tr("Next Logo"), this);
    logoLayout->addWidget(nextLogoButton);
    connect(nextLogoButton, &QPushButton::clicked, [=]() {
        RotateImage(logoLabel, logos, logoIndex, "logo");
    });

    //Icons
    auto iconLayout = new QVBoxLayout();
    art_line_two->addLayout(iconLayout);
    icons = SteamGridDb::getIcons(0);
    iconIndex = 0;
    iconLabel = new QLabel(this);
    iconLabel->setScaledContents(true);
    iconLabel->setFixedWidth(94);
    iconLabel->setFixedHeight(94);
    iconLayout->addWidget(iconLabel);
    loadImage(iconLabel, icons, iconIndex);
    auto nextIconButton = new QPushButton(tr("Next Icon"), this);
    iconLayout->addWidget(nextIconButton);
    connect(nextIconButton, &QPushButton::clicked, [=]() {
        RotateImage(iconLabel, icons, iconIndex, "icon");
    });

    auto action_line = new QHBoxLayout();
    vertical_layout->addLayout(action_line);

    create_shortcut_button = new QPushButton(tr("Add to Steam"), this);
    action_line->addWidget(create_shortcut_button);
    // connect(create_shortcut_button, &QPushButton::clicked, this, &ShortcutDialog::CreateShortcut);
    connect(create_shortcut_button, &QPushButton::clicked, [=]() {
        std::map<std::string, std::string> artwork;
        artwork["landscape"] = landscapes.at(landscapeIndex);
        artwork["portrait"] = portraits.at(portraitIndex);
        artwork["hero"] = heroes.at(heroIndex);
        artwork["logo"] = logos.at(logoIndex);
        artwork["icon"] = icons.at(iconIndex);
        CreateShortcut(artwork);
    });
}

void ShortcutDialog::loadImage(QLabel* label, std::vector<std::string> images, int index) {
    std::string url = images.at(index);
    ImageLoader imageLoader(label);
    imageLoader.loadImage(QString::fromStdString(url));
}

void ShortcutDialog::RotateImage(QLabel* label, std::vector<std::string>& images, int& index, std::string type) {
    index++;
    //If we've run out of images
    if (index >= images.size()) {
        index = 0;
    }
    loadImage(label, images, index);
}

void ShortcutDialog::ExternalChanged() {
    external_group_box->setVisible(allow_external_access_checkbox->isChecked());
}

void ShortcutDialog::CreateShortcut(std::map<std::string, std::string> artwork) {
    //Create a map of variables for the template
    std::map<std::string, std::string> paramMap;

    paramMap["home_ssid"] = local_ssid_edit->text().toStdString();
    paramMap["ps_console"] = server->IsPS5() ? "5" : "4";
    paramMap["wait_timeout"] = "35";
    paramMap["home_addr"] = server->GetHostAddr().toStdString();
    paramMap["away_addr"] = external_dns_edit->text().toStdString();
    paramMap["regist_key"] = server->registered_host.GetRPRegistKey().toStdString();
    paramMap["login_passcode"] = passcode_edit->text().toStdString();
    paramMap["mode"] = mode_combo_box->currentText().toStdString();
    paramMap["server_nickname"] = server->discovered ? server->discovery_host.host_name.toStdString() : server->registered_host.GetServerNickname().toStdString();

    std::string fileText = compileTemplate("shortcut.tmpl", paramMap);

    if (allow_external_access_checkbox->isChecked()) {
        if (server->IsPS5()) {
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

    std::string filePath;
    filePath.append(getenv("HOME"));
    filePath.append("/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/");
    filePath.append(paramMap["server_nickname"]);
    filePath.append(".sh");
    VDFParser::createDirectories(filePath);

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

    AddToSteam(server, filePath, artwork);

    QMessageBox::information(nullptr, "Success", QString::fromStdString("Added "+paramMap["server_nickname"]+" to Steam"), QMessageBox::Ok);

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
    std::vector<std::map<std::string, std::string>> shortcuts = VDFParser::parseShortcuts();

    std::map<std::string, std::string> newShortcut = VDFParser::buildShortcutEntry(server, filePath, artwork);

    bool found = false;
    //Look to see if we need to update
    for (auto& map : shortcuts) {
        // Check if the key exists and its value matches the valueToSearch
        auto it = map.find("Exe");
        if (it != map.end() && it->second == VDFParser::getValueFromMap(newShortcut, "Exe")) {
            // Replace the entire map with the new one
            std::cout << "Updating Steam entry" << std::endl;
            map = newShortcut;
            found = true;
            break;  // Stop iterating once a match is found
        }
    }

    //If we didn't find it to update, let's add it to the end
    if (!found) {
        std::cout << "Adding Steam entry" << std::endl;
        shortcuts.emplace_back(VDFParser::buildShortcutEntry(server, filePath, artwork));
    }
    VDFParser::updateShortcuts(shortcuts);
}