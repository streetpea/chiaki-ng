#include "steamtools.h"

#include <fstream>
#include <qfileinfo.h>
#include <qtextstream.h>
#include <sstream>
#include <sys/stat.h>

#include "crc.h"
#include "vdfstatemachine.h"
#include "vdf_parser.hpp"

//Header of the shortcut file, used to check validity
std::vector<char> fileHeader = { 0x00, 0x73, 0x68, 0x6F, 0x72, 0x74, 0x63, 0x75, 0x74, 0x73, 0x00 };

SteamTools::SteamTools(const std::function<void(const QString&)>& infoFunction, const std::function<void(const QString&)>& errorFunction): infoFunction(infoFunction),
    errorFunction(errorFunction) {

    //These won't change so set them once when instantiating the object
    steamBaseDir = getSteamBaseDir();
    mostRecentUser = getMostRecentUser();
    shortcutFile = getShortcutFile();
}

/**
 * \brief Get the base directory for the steam installation as a QString
 * \return QString for the steam base directory
 */
QString SteamTools::getSteamBaseDir() {
    QString steamBaseDir;
    #if defined(__APPLE__)
        steamBaseDir.append(getenv("HOME"));
        steamBaseDir.append("/Library/Application Support/Steam");
    #elif defined(__linux__)
        std::string steamFlatpakDir = "";
        steamBaseDir.append(getenv("HOME"));

        steamFlatpakDir.append(getenv("HOME"));
        steamFlatpakDir.append("/.var/app/com.valvesoftware.Steam/data/Steam");

        // If flatpak Steam is installed
        if (access(steamFlatpakDir.c_str(), 0) == 0) {
            steamBaseDir.append("/.var/app/com.valvesoftware.Steam/data/Steam");
        }
        else {
            // Steam installed on host
            steamBaseDir.append("/.steam/steam");
        }
    #endif
        return steamBaseDir;
}

/**
 * \brief Read the loginusers.vdf from the steam directory to find the most recently logged in user
 * \return the User's ID as a QString
 */
QString SteamTools::getMostRecentUser() {
    QString steamid;
    QString user_id;

    //Get the loginUsers file
    QString steamConfigFilePath = QString("%1/config/loginusers.vdf").arg(steamBaseDir);
    QFile steamConfigfile(steamConfigFilePath);

    //Open the file and print any errors
    if (!steamConfigfile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorFunction(QString("Failed to open loginusers.vdf: %1").arg(steamConfigfile.errorString()));
        return nullptr;
    }

    // Create a QTextStream to read from the file
    QTextStream in(&steamConfigfile);

    // Read the file line by line
    while (!in.atEnd()) {
        QString line = in.readLine();

        if (line.contains("7656119") && !line.contains("PersonalName")) {
            steamid = line.mid(line.indexOf("7656119"), line.size() - 1);
        } else if ((line.contains("mostrecent", Qt::CaseInsensitive) || line.contains("MostRecent")) &&
           line.contains("\"1\"")) {
            unsigned long long steamidLongLong = atoll(steamid.toStdString().c_str());
            steamidLongLong -= 76561197960265728;
            user_id = QString::fromStdString(std::to_string(steamidLongLong));
        }
    }

    // Close the file
    steamConfigfile.close();

    return user_id;
}

/**
 * \brief Get path to shortcuts.vdf as a QString using steam base dir and current user
 * \return QString of path to shortcuts.vdf
 */
QString SteamTools::getShortcutFile() {
    return QString("%1/userdata/%2/config/shortcuts.vdf")
        .arg(steamBaseDir)
        .arg(mostRecentUser);
}

/**
 * \brief Can we find steam?
 * \return bool if we can find steam
 */
bool SteamTools::steamExists() {
    QString directoryPath = steamBaseDir;
    #ifdef _WIN32
        DWORD attributes = GetFileAttributes(directoryPath.toStdString().c_str());
        return (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY));
    #elif __linux__ || __APPLE__
        struct stat info;
        return (stat(directoryPath.toStdString().c_str(), &info) == 0 && S_ISDIR(info.st_mode));
    #else
        // Unsupported platform, you may need to add platform-specific code here
        return false;
    #endif
}

/**
 * \brief Check the first n characters in two vectors and confirm they are the same
 * \param size number of characters to check
 * \param vector1 First Vector
 * \param vector2 Second Vector
 * \return boolean of whether the first n characters match
 */
bool compareFirstElements(int size, const std::vector<char>& vector1, const std::vector<char>& vector2) {
    // Check if both vectors have at least the correct number of elements
    if (vector1.size() >= size && vector2.size() >= size) {
        // Use std::equal to compare the first n elements
        return std::equal(vector1.begin(), vector1.begin() + size, vector2.begin());
    }
    // Vectors don't have enough elements for comparison
    return false;
}

/**
 * \brief Create a list of SteamShortcutEntries for all the shortcuts currently in shortcut.vdf
 * \return a QVector of SteamShortcutEntry for each existing non-steam shortcut
 */
QVector<SteamShortcutEntry> SteamTools::parseShortcuts() {
    QVector<SteamShortcutEntry> shortcuts;

    std::ifstream file(shortcutFile.toStdString(), std::ios::binary);

        if (file.is_open()) {
            // Determine the size of the file
            file.seekg(0, std::ios::end);
            std::streampos fileSize = file.tellg();
            file.seekg(0, std::ios::beg);

            if (fileSize < 16) {
                infoFunction("shortcut file not valid");
                return shortcuts;
            }

            // Create a buffer to hold the file data
            std::vector<char> buffer(fileSize);

            // Read the file data into the buffer
            file.read(buffer.data(), fileSize);

            // Close the file
            file.close();

            if (!compareFirstElements(11, buffer, fileHeader)) {
                infoFunction("shortcut file not valid, incorrect header");
                return shortcuts;
            }

            buffer.erase(buffer.begin(), buffer.begin() + 11);

            VDFStateMachine::ParseState state = VDFStateMachine::ParseState::APPID;
            VDFStateMachine::ListParseState listState = VDFStateMachine::ListParseState::WAITING;
            VDFStateMachine::FieldType type;
            std::ostringstream utf8String;
            QString key;
            SteamShortcutEntry entry;
            std::vector<char> endingBuffer;
            QStringList listValue;

            for (char byte: buffer) {
                // Convert the byte to an unsigned integer (uint8)
                uint8_t value = static_cast<uint8_t>(byte);

                switch (state) {
                    case VDFStateMachine::ParseState::APPID:
                        VDFStateMachine::APPID::handleState(value, state, type);
                    break;
                    case VDFStateMachine::ParseState::WAITING:
                        VDFStateMachine::WAITING::handleState(value, state, type, listValue);
                    break;
                    case VDFStateMachine::ParseState::KEY:
                        VDFStateMachine::KEY::handleState(value, state, type, utf8String, key);
                    break;
                    case VDFStateMachine::ParseState::VALUE:
                        VDFStateMachine::VALUE::handleState(value, state, type, utf8String, key, entry, listState,
                                                            listValue, endingBuffer, shortcuts);
                    break;
                    case VDFStateMachine::ParseState::ENDING:
                        VDFStateMachine::ENDING::handleState(value, state, type, endingBuffer);
                }
            }
        } else {
            errorFunction(QString("Error opening file: %1").arg(shortcutFile));
        }

        return shortcuts;
}

/**
 * \brief Open a Text file and count how many times a provided string is found
 * \param infoFunction Lambda function for printing info to a log file
 * \param filePath The path to the file to check
 * \param subString The string to look for
 * \return number of times the subString appears in the body of the file
 */
int countStringOccurrencesInFile(std::function<void(const QString&)> &infoFunction, QString filePath, QString subString) {
    // Read the contents of the file into a QString
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        infoFunction(QString("Error opening file: %1").arg(file.errorString()));
        return 0;
    }

    QTextStream in(&file);
    QString mainString = in.readAll();

    file.close();

    // Count occurrences of subString in mainString
    return mainString.count(subString);
}

/**
 * \brief Given a map of Pixmaps, save them to the locations provided in a artworkLocation map
 * The artworkLocation map provides a QString template for where to save each image to
 * \param shortAppId The name of the app whose artwork to save
 * \param artwork Map of artwork type -> Pixmap
 * \param artworkLocations Map of Artwork type -> QString template for save location
 */
void SteamTools::saveArtwork(QString shortAppId, QMap<QString, const QPixmap*> artwork, QMap<QString, QString> artworkLocations) {
    for (auto it = artwork.begin(); it != artwork.end(); ++it) {
        QString saveLocation = artworkLocations.value(it.key())
            .arg(steamBaseDir)
            .arg(mostRecentUser)
            .arg(shortAppId);
        it.value()->save(saveLocation);
        infoFunction("Saved "+it.key()+" to "+saveLocation);
    }
}

/**
 * \brief Create a SteamShortcutEntry for the given app with supplied path and artwork
 * \param appName The name of the shortcut to add
 * \param filepath The path to the Exe of the ap
 * \param artwork Map of Artwork Type -> Pixmap
 * \return A SteamShortcutEntry crafted from these input parameters
 */
SteamShortcutEntry SteamTools::buildShortcutEntry(QString appName, QString filepath, QMap<QString, const QPixmap*> artwork) {

    SteamShortcutEntry entry;
    QString shortAppId = generateShortAppId("\"" + filepath + "\"", appName);
    auto entryID = countStringOccurrencesInFile(errorFunction, shortcutFile, "appid");
    entryID++;

    //Handle Artwork
    QMap<QString, QString> artworkLocations = {
        {"icon", "%1/userdata/%2/config/grid/%3_icon.png"},
        {"landscape", "%1/userdata/%2/config/grid/%3.png"},
        {"portrait", "%1/userdata/%2/config/grid/%3p.png"},
        {"hero", "%1/userdata/%2/config/grid/%3_hero.png"},
        {"logo", "%1/userdata/%2/config/grid/%3_logo.png"},
    };

    saveArtwork(shortAppId, artwork, artworkLocations);
    if (artwork.contains("icon")) {
        QString iconPath = artworkLocations.value("icon")
                .arg(steamBaseDir)
                .arg(mostRecentUser)
                .arg(shortAppId);
        entry.setProperty("icon", iconPath);
    }
    
    entry.setProperty("appid", QString::number(entryID));
    entry.setProperty("AppName", appName);
    entry.setProperty("Exe", "\"" + filepath + "\"");
    QFileInfo fileInfo(filepath);
    QString directoryPath = fileInfo.absolutePath();
    entry.setProperty("StartDir", directoryPath);

    entry.setProperty("ShortcutPath", "");
    entry.setProperty("LaunchOptions", "");
    entry.setProperty("IsHidden", "");
    entry.setProperty("AllowDesktopConfig", "");
    entry.setProperty("AllowOverlay", "");
    entry.setProperty("OpenVR", "");
    entry.setProperty("Devkit", "");
    entry.setProperty("DevkitGameID", "");
    entry.setProperty("DevkitOverrideAppID", "");
    entry.setProperty("LastPlayTime", "+ne");
    entry.setProperty("FlatpakAppID", "");

    return entry;
}

/**
 * \brief write this shortcut attribute to a file
 * \param outFile The file to write to
 * \param type The type of Attribute
 * \param key The name of the Attribute
 * \param value The value of the Attribute
 */
void writeShortcutAttribute(std::ofstream& outFile, VDFStateMachine::FieldType type, QString key, QString value) {
    // Write the identifying bit
    if (type == VDFStateMachine::FieldType::LIST) {
        outFile.put(0x00);
    } else if (type == VDFStateMachine::FieldType::STRING) {
        outFile.put(0x01);
    } else {
        outFile.put(0x02);
    }

    // Write the key
    outFile << key.toStdString();

    // Write the divider
    outFile.put(0x00);

    // Write the value based on the type
    if (type == VDFStateMachine::FieldType::STRING) {
        outFile << value.toStdString();
        // Ending sequence for string
        outFile.put(0x00);
    } else if (type == VDFStateMachine::FieldType::BOOLEAN) {
        // Boolean value encoding (0x01 for true, 0x00 for false)
        outFile.put(value == "true" ? 0x01 : 0x00);
        // Ending sequence for boolean
        outFile.put(0x00).put(0x00).put(0x00);
    } else if (type == VDFStateMachine::FieldType::DATE) {
        // Date encoding (0x00 0x00 0x00 0x00)
        outFile.put(0x00).put(0x00).put(0x00).put(0x00);
    } else if (type == VDFStateMachine::FieldType::LIST) {
        QStringList list = value.split(",");
        int index = 0;
        for (const auto& element: list) {
            // Index for list entry (incremented manually in actual usage)
            outFile.put(0x01);
            // Contents of the list entry
            outFile << index;
            // Divider for multiple entries
            outFile.put(0x00);
            // Entry
            outFile << element.toStdString();
            // Ending sequence for list
            outFile.put(0x00);
            // increment index
            index++;
        }
        outFile.put(0x08).put(0x08);
    }
}

/**
 * \brief Update shortcuts.vdf to contain the full list of shortcuts provied
 * \param shortcuts all the shortcuts to include in shortcuts.vdf
 */
void SteamTools::updateShortcuts(QVector<SteamShortcutEntry> shortcuts) {
        QFileInfo fileInfo(shortcutFile);
        QString directoryPath = fileInfo.absolutePath();

        QString backupFile = directoryPath;
        backupFile.append("shortcuts.vdf.bak");
        QFile::copy(shortcutFile, backupFile);

        // Open the file for binary writing
        std::ofstream outFile(shortcutFile.toStdString(), std::ios::binary);

        // Check if the file is open
        if (outFile.is_open()) {
            std::vector<uint8_t> byteData;

            outFile.put(0x00);
            outFile << "shortcuts";
            outFile.put(0x00);

            int appId = 0;
            for (SteamShortcutEntry shortcut: shortcuts) {
                outFile.put(0x00);
                outFile << std::to_string(appId);
                outFile.put(0x00).put(0x02);
                outFile << "appid";
                outFile.put(0x00);

                QString exe = shortcut.getExe();
                QString appName = shortcut.getAppName();
                uint32_t shortcutId = generateShortcutId(exe, appName);
                outFile.write(reinterpret_cast<const char *>(&shortcutId), sizeof(shortcutId));

                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::STRING, "AppName", shortcut.getAppName());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::STRING, "Exe", shortcut.getExe());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::STRING, "StartDir",
                              shortcut.getStartDir());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::STRING, "icon", shortcut.geticon());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::STRING, "ShortcutPath",
                              shortcut.getShortcutPath());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::STRING, "LaunchOptions",
                              shortcut.getLaunchOptions());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::BOOLEAN, "IsHidden",
                              shortcut.getIsHidden());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::BOOLEAN, "AllowDesktopConfig",
                              shortcut.getAllowDesktopConfig());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::BOOLEAN, "AllowOverlay",
                              shortcut.getAllowOverlay());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::BOOLEAN, "OpenVR", shortcut.getOpenVR());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::BOOLEAN, "Devkit", shortcut.getDevkit());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::STRING, "DevkitGameID",
                              shortcut.getDevkitGameID());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::BOOLEAN, "DevkitOverrideAppID",
                              shortcut.getDevkitOverrideAppID());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::DATE, "LastPlayTime",
                              shortcut.getLastPlayTime());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::STRING, "FlatpakAppID",
                              shortcut.getFlatpakAppID());
                writeShortcutAttribute(outFile, VDFStateMachine::FieldType::LIST, "tags", "");
                appId++;
            }

            outFile.put(0x08).put(0x08);
            outFile.write(reinterpret_cast<char *>(byteData.data()), byteData.size());

            // Close the file
            outFile.close();
            infoFunction(QString("File '%1' created successfully.").arg(shortcutFile));
        } else {
            errorFunction(QString("Error opening file '%1' for writing.").arg(shortcutFile));
        }
}

/**
 * \brief Set the neptune controller config for a given appName
 * \param appName The name of the App
 * \param controllerConfigID The controller Config ID
 */
void SteamTools::updateControllerConfig(QString appName, QString controllerConfigID) {
        QString controllerFile = QString("%1/steamapps/common/Steam Controller Configs/%2/config/configset_controller_neptune.vdf")
            .arg(steamBaseDir)
            .arg(mostRecentUser);
        QFileInfo controllerFileInfo(controllerFile);

        if (!controllerFileInfo.exists()) {
            errorFunction("Neptune controller config not found, not adding");
            return;
        }

        // Convert the string to lowercase
        appName = appName.toLower();

        std::ifstream file(controllerFile.toStdString());
        auto root = tyti::vdf::read(file);
        auto existingRecord = root.childs.find(appName.toStdString());

        if (existingRecord == root.childs.end()) {
            infoFunction(QString("No controller config found for %s so adding in chiaki4deck workshop id %1").arg(appName));
            tyti::vdf::basic_object<std::ifstream::char_type> controllerEntry;
            controllerEntry.set_name(appName.toStdString());
            controllerEntry.add_attribute("workshop", controllerConfigID.toStdString());

            std::unique_ptr<tyti::vdf::basic_object<std::ifstream::char_type>> uniqueObjectPtr(new tyti::vdf::basic_object<char>(controllerEntry));

            root.add_child(std::move(uniqueObjectPtr));
            std::ofstream outFile(controllerFile.toStdString());
            QString backupFile = QString("%1.bak").arg(controllerFile);
            QFile::copy(controllerFile, backupFile);
            tyti::vdf::write(outFile, root);
        } else {
            infoFunction(QString("Controller config already set for %1, not overwriting").arg(appName));
        }
}

uint64_t generatePreliminaryId(const std::string& exe, const std::string& appname) {
    std::string key = exe + appname;

    // Convert the CRC32 result to a BigInt
    crc_t crcResult;

    crcResult = crc_init();
    crcResult = crc_update(crcResult, (unsigned char *) key.c_str(), strlen(key.c_str()));
    crcResult = crc_finalize(crcResult);

    uint64_t top = static_cast<uint64_t>(crcResult) | static_cast<uint64_t>(0x80000000);

    // Perform bitwise operations
    uint64_t preliminaryId = (top << 32) | static_cast<uint64_t>(0x02000000);

    return preliminaryId;
}

// Function to generate App ID
std::string generateAppId(const std::string& exe, const std::string& appname) {
    return std::to_string(generatePreliminaryId(exe, appname));
}

// Function to shorten App ID
std::string shortenAppId(const std::string& longId) {
    return std::to_string(std::stoull(longId) >> 32);
}

QString SteamTools::generateShortAppId(QString exe, QString appname) {
    std::string longId = generateAppId(exe.toStdString(), appname.toStdString());
    return QString::fromStdString(shortenAppId(longId));
}

uint32_t SteamTools::generateShortcutId(QString exe, QString appname) {
    // Generate preliminary ID
    uint64_t preliminaryId = generatePreliminaryId(exe.toStdString(), appname.toStdString());

    // Shift right by 32 bits and subtract 0x100000000
    uint32_t shortcutId = static_cast<uint32_t>((preliminaryId >> 32) - 0x100000000);

    return shortcutId;
}
