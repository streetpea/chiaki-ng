#include "../include/shortcutdialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QPushButton>
#include <iostream>
#include <fstream>
#include <QFileDialog>
#include <qtextstream.h>
#include <regex>
#include <string>

ShortcutDialog::ShortcutDialog(const DisplayServer *server, QWidget* parent) {
    this->server = server;
    const std::string windowTitle{"Create Shortcut "+(server->discovered ? server->discovery_host.host_name.toStdString() : server->registered_host.GetServerNickname().toStdString())};
    setWindowTitle(tr(windowTitle.c_str()));

    const auto general_layout = new QFormLayout();
    setLayout(general_layout);
    if(general_layout->spacing() < 16)
        general_layout->setSpacing(16);

    allow_external_access_checkbox = new QCheckBox(this);
    general_layout->addRow(tr("Access this Playstation externally?"), allow_external_access_checkbox);
    connect(allow_external_access_checkbox, &QCheckBox::stateChanged, this, &ShortcutDialog::ExternalChanged);

    external_dns_edit = new QLineEdit(this);
    general_layout->addRow(tr("External IP Address or DNS Entry"), external_dns_edit);
    external_dns_edit->setEnabled(false);

    local_ssid_edit = new QLineEdit(this);
    general_layout->addRow(tr("What's the SSID of your home wifi?"), local_ssid_edit);
    local_ssid_edit->setEnabled(false);
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
    general_layout->addRow(tr("Mode: "), mode_combo_box);

    passcode_edit = new QLineEdit(this);
    general_layout->addRow(tr("4 Digit passcode (optional):"), passcode_edit);

    create_shortcut_button = new QPushButton(tr("Create"), this);
    general_layout->addWidget(create_shortcut_button);
    connect(create_shortcut_button, &QPushButton::clicked, this, &ShortcutDialog::CreateShortcut);
}

void ShortcutDialog::ExternalChanged() {
    external_dns_edit->setEnabled(allow_external_access_checkbox->isChecked());
    local_ssid_edit->setEnabled(allow_external_access_checkbox->isChecked());
}

void ShortcutDialog::CreateShortcut() {
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

    QString filePath = QFileDialog::getSaveFileName(nullptr, "Save File", QString::fromStdString(paramMap["server_nickname"]+".sh"), "Shell Scripts (*.sh)");

    // Check if the user canceled the dialog
    if (filePath.isEmpty()) {
        return;
    }

    // Open the file for writing
    QFile outputFile(filePath);
    if (outputFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        // Write the string to the file
        QTextStream out(&outputFile);
        out << QString::fromStdString(fileText);

        // Close the file
        outputFile.close();
    }

    // Construct the shell command to make the file executable
    const std::string chmodCommand = "chmod +x " + filePath.toStdString();

    // Execute the shell command to make it executable
    std::system(chmodCommand.c_str());

    ShortcutDialog::close();
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
    #elif defined(__linux__) || defined(__APPLE__)
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