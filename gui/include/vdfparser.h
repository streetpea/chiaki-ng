#ifndef VDFPARSER_H
#define VDFPARSER_H
#include <fstream>
#include <string>
#include <crc.h>
#include <vdfstatemachine.h>

#include <iostream>
#include <curl/curl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32)
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#else
#include <sys/stat.h>
#endif

namespace VDFParser {

    static std::vector<char> fileHeader = { 0x00, 0x73, 0x68, 0x6F, 0x72, 0x74, 0x63, 0x75, 0x74, 0x73, 0x00 };

    // Callback function to write data to a file
    size_t WriteCallback(void* contents, size_t size, size_t nmemb, FILE* stream) {
        return fwrite(contents, size, nmemb, stream);
    }

    void createDirectories(const std::string& path) {
        size_t pos = 0;
        while ((pos = path.find_first_of("/\\", pos + 1)) != std::string::npos) {
            std::string subPath = path.substr(0, pos);

            // Check if the directory already exists
            struct stat info;
            if (stat(subPath.c_str(), &info) != 0 || !(info.st_mode & S_IFDIR)) {
                // If not, create the directory
                if (mkdir(subPath.c_str(), 0777) != 0) {
                    std::cerr << "Error creating directory: " << subPath << std::endl;
                    return;
                }
                std::cout << "Created directory: " << subPath << std::endl;
            }
        }
    }

    // Function to download a file from a URL to a destination directory
    bool downloadFile(const char* url, const char* destPath) {
        // Extract the directory from the destination path
        std::string destDir = destPath;
        size_t lastSlash = destDir.find_last_of('/');
        if (lastSlash != std::string::npos) {
            destDir = destDir.substr(0, lastSlash);
        }

        // Ensure the destination directory exists
        createDirectories(destPath);

        // Check if the destination file already exists
        // struct stat fileStat;
        // if (stat(destPath, &fileStat) == 0) {
        //     std::cout << "File already exists at: " << destPath << std::endl;
        //     return true; // File already exists, no need to download
        // }

        // Open the file for writing
        FILE* file = fopen(destPath, "wb");
        if (!file) {
            std::cerr << "Error opening file for writing: " << strerror(errno) << std::endl;
            return false;
        }

        // Initialize libcurl
        CURL* curl = curl_easy_init();
        if (!curl) {
            std::cerr << "Error initializing libcurl." << std::endl;
            fclose(file);
            return false;
        }

        // Set the URL to download
        curl_easy_setopt(curl, CURLOPT_URL, url);

        // Set the callback function to write data to the file
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

        // Perform the download
        CURLcode res = curl_easy_perform(curl);

        // Check for errors
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            fclose(file);
            curl_easy_cleanup(curl);
            return false;
        }

        // Clean up and close the file
        fclose(file);
        curl_easy_cleanup(curl);

        std::cout << "File downloaded successfully to: " << destPath << std::endl;
        return true;
    }

    inline void downloadAssets(std::string gridPath, std::string shortAppId, std::map<std::string, std::string> artwork) {
        std::string landscapeGridPath;
        landscapeGridPath.append(gridPath);
        landscapeGridPath.append(shortAppId);
        landscapeGridPath.append(".png");
        downloadFile(artwork["landscape"].c_str(), landscapeGridPath.c_str());

        std::string portraitGridPath;
        portraitGridPath.append(gridPath);
        portraitGridPath.append(shortAppId);
        portraitGridPath.append("p.png");
        downloadFile(artwork["portrait"].c_str(), portraitGridPath.c_str());

        std::string heroPath;
        heroPath.append(gridPath);
        heroPath.append(shortAppId);
        heroPath.append("_hero.png");
        downloadFile(artwork["hero"].c_str(), heroPath.c_str());

        std::string logoPath;
        logoPath.append(gridPath);
        logoPath.append(shortAppId);
        logoPath.append("_logo.png");
        downloadFile(artwork["logo"].c_str(), logoPath.c_str());
    }

    inline uint64_t generatePreliminaryId(const std::string& exe, const std::string& appname) {
        std::string key = exe + appname;

        // Convert the CRC32 result to a BigInt
        crc_t crcResult;

        crcResult = crc_init();
        crcResult = crc_update(crcResult, (unsigned char *)key.c_str(), strlen(key.c_str()));
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

    // Function to generate Short App ID
    std::string generateShortAppId(const std::string& exe, const std::string& appname) {
        std::string longId = generateAppId(exe, appname);
        return shortenAppId(longId);
    }

    inline uint32_t generateShortcutId(const std::string& exe, const std::string& appname) {
        // Generate preliminary ID
        uint64_t preliminaryId = generatePreliminaryId(exe, appname);

        // Shift right by 32 bits and subtract 0x100000000
        uint32_t shortcutId = static_cast<uint32_t>((preliminaryId >> 32) - 0x100000000);

        return shortcutId;
    }

    inline std::string getValueFromMap(const std::map<std::string, std::string>& myMap, const std::string& key) {
        auto it = myMap.find(key);
        return (it != myMap.end()) ? it->second : ""; // Return the value or an empty string if the key is not found
    }

    inline bool containsString(const std::vector<std::string>& stringArray, const std::string& targetString) {
        auto it = std::find(stringArray.begin(), stringArray.end(), targetString);
        return it != stringArray.end();
    }

    bool compareFirstElements(int size, const std::vector<char>& vector1, const std::vector<char>& vector2) {
        // Check if both vectors have at least 11 elements
        if (vector1.size() >= size && vector2.size() >= size) {
            // Use std::equal to compare the first 11 elements
            return std::equal(vector1.begin(), vector1.begin() + size, vector2.begin());
        } else {
            // Vectors don't have enough elements for comparison
            return false;
        }
    }

    inline std::vector<std::string> splitString(std::string input, char delimiter) {
        std::vector<std::string> retVector;

        std::stringstream ss(input);
        std::string token;

        while (std::getline(ss, token, delimiter)) {
            retVector.push_back(token);
        }

        return retVector;
    }

    inline int countStringOccurrencesInFile(const std::string& filename, const std::string& targetString) {
        std::ifstream file(filename);

        if (!file.is_open()) {
            std::cerr << "Error opening file: " << filename << std::endl;
            return -1; // Return -1 to indicate an error
        }

        std::string line;
        int count = 0;

        while (std::getline(file, line)) {
            size_t pos = 0;
            while ((pos = line.find(targetString, pos)) != std::string::npos) {
                ++count;
                pos += targetString.length();
            }
        }

        file.close();

        return count;
    }

    inline std::string getDirectoryFromPath(std::string fullPath) {
        // Extract the directory from the destination path
        size_t lastSlash = fullPath.find_last_of('/');
        if (lastSlash != std::string::npos) {
            fullPath = fullPath.substr(0, lastSlash-1);
        }
        return fullPath;
    }

    inline void copyFile(const std::string& sourcePath, const std::string& destinationPath) {
        // Open the source file for binary input
        std::ifstream sourceFile(sourcePath, std::ios::binary);

        if (!sourceFile.is_open()) {
            std::cerr << "Error opening the source file: " << sourcePath << std::endl;
            return;
        }

        // Open the destination file for binary output
        std::ofstream destinationFile(destinationPath, std::ios::binary);

        if (!destinationFile.is_open()) {
            std::cerr << "Error opening the destination file: " << destinationPath << std::endl;
            sourceFile.close(); // Close the source file
            return;
        }

        // Copy the contents of the source file to the destination file
        destinationFile << sourceFile.rdbuf();

        // Close both files
        sourceFile.close();
        destinationFile.close();

        std::cout << "File copied successfully from " << sourcePath << " to " << destinationPath << std::endl;
    }

    inline std::string getSteamBaseDir() {
        std::string steamBaseDir;
        #if defined(__APPLE__)
            steamBaseDir.append(getenv("HOME"));
            steamBaseDir.append("/Library/Application Support/Steam");
        #elif defined(__linux__)
            std::string steamFlatpakDir = "";
            steamBaseDir.append(getenv("HOME"));

            steamFlatpakDir.append(getenv("HOME"));
            steamFlatpakDir.append("/.var/app/com.valvesoftware.Steam/data/Steam");

            // If flatpaked Steam is installed
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

    inline std::string getMostRecentUser() {
        std::string steamid = "";
        std::string user_id;
        std::string steamBaseDir = getSteamBaseDir();

        std::string steamConfigFile;
        steamConfigFile.append( steamBaseDir);
        steamConfigFile.append("/config/loginusers.vdf");

        // Open a file for reading
        std::ifstream inputFile(steamConfigFile);

        // Check if the file is open
        if (!inputFile.is_open()) {
            std::cerr << "Error opening the file." << std::endl;
            return nullptr;
        }

        // Read and print each line from the file
        std::string line;
        while (std::getline(inputFile, line)) {
            if (strstr(line.c_str(), "7656119") && !strstr(line.c_str(), "PersonaName")) {
                steamid = "";
                steamid.append(strstr(line.c_str(), "7656119"));
                steamid.erase(steamid.size() - 1);
            }
            else if ((strstr(line.c_str(), "mostrecent") || strstr(line.c_str(), "MostRecent")) && strstr(line.c_str(), "\"1\"")) {
                unsigned long long steamidLongLong = atoll(steamid.c_str());
                steamidLongLong -= 76561197960265728;
                user_id = std::to_string(steamidLongLong);
            }
        }

        // Close the file
        inputFile.close();

        return user_id;
    }

    inline std::string getShortcutFile() {
        std::string shortcutFile = "";
        shortcutFile.append(getSteamBaseDir());
        shortcutFile.append("/userdata/");
        shortcutFile.append(getMostRecentUser());
        shortcutFile.append("/config/shortcuts.vdf");

        return shortcutFile;
    }

    inline std::vector<std::map<std::string, std::string>> parseShortcuts() {
        std::vector<std::map<std::string, std::string>> shortcuts;
        std::string shortcutFile = getShortcutFile();

        std::ifstream file(shortcutFile, std::ios::binary);

        if (file.is_open()) {
            // Determine the size of the file
            file.seekg(0, std::ios::end);
            std::streampos fileSize = file.tellg();
            file.seekg(0, std::ios::beg);

            if (fileSize < 16) {
                std::cout << "shortcut file not valid" << std::endl;
                return shortcuts;
            }

            // Create a buffer to hold the file data
            std::vector<char> buffer(fileSize);

            // Read the file data into the buffer
            file.read(buffer.data(), fileSize);

            // Close the file
            file.close();

            if(!compareFirstElements(11, buffer, fileHeader)) {
                std::cout << "shortcut file not valid, incorrect header" << std::endl;
                return shortcuts;
            }

            buffer.erase(buffer.begin(), buffer.begin() + 11);

            VDFStateMachine::ParseState state = VDFStateMachine::ParseState::APPID;
            VDFStateMachine::ListParseState listState = VDFStateMachine::ListParseState::WAITING;
            VDFStateMachine::FieldType type;
            std::ostringstream utf8String;
            std::string key;
            std::map<std::string, std::string> entry;
            std::vector<char> endingBuffer;
            std::vector<std::string> listValue;

            for (char byte : buffer) {
                // Convert the byte to an unsigned integer (uint8)
                uint8_t value = static_cast<uint8_t>(byte);

                switch(state) {
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
                        VDFStateMachine::VALUE::handleState(value, state, type, utf8String, key, entry, listState, listValue, endingBuffer, shortcuts);
                    break;
                    case VDFStateMachine::ParseState::ENDING:
                        VDFStateMachine::ENDING::handleState(value, state, type, endingBuffer);
                }
            }

        } else {
            std::cerr << "Error opening file: " << shortcutFile << std::endl;
        }

        return shortcuts;
    }

    inline void writeShortcut(std::ofstream& outFile, VDFStateMachine::FieldType type, const std::string key, const std::string value) {
        // Write the identifying bit
        if (type == VDFStateMachine::FieldType::LIST) {
            outFile.put(0x00);
        } else if (type == VDFStateMachine::FieldType::STRING) {
            outFile.put(0x01);
        } else {
            outFile.put(0x02);
        }

        // Write the key
        outFile << key;

        // Write the divider
        outFile.put(0x00);

        // Write the value based on the type
        if (type == VDFStateMachine::FieldType::STRING) {
            outFile << value;
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
            std::vector<std::string> list = splitString(value, ',');
            int index = 0;
            for (const auto& element : list) {
                // Index for list entry (incremented manually in actual usage)
                outFile.put(0x01);
                // Contents of the list entry
                outFile << index;
                // Divider for multiple entries
                outFile.put(0x00);
                // Entry
                outFile << element;
                // Ending sequence for list
                outFile.put(0x00);
                // increment index
                index++;
            }
            outFile.put(0x08).put(0x08);
        }
    }

    inline std::map<std::string, std::string> buildShortcutEntry(const DisplayServer* server, std::string filepath, std::map<std::string, std::string> artwork) {
        std::string shortcutFile = getShortcutFile();
        std::string appName = server->registered_host.GetServerNickname().toStdString();

        std::string shortAppId = generateShortAppId("\""+filepath+"\"", appName);

        int entryID = countStringOccurrencesInFile(shortcutFile, "appid");
        entryID++;

        //Download the icon
        std::string gridPath;
        std::string iconPath;

        gridPath.append(getSteamBaseDir());
        gridPath.append("/userdata/");
        gridPath.append(getMostRecentUser());
        gridPath.append("/config/grid/");
        iconPath.append(gridPath);
        iconPath.append(shortAppId);
        iconPath.append("_icon.png");
        downloadFile(artwork["icon"].c_str(), iconPath.c_str());

        downloadAssets(gridPath, shortAppId, artwork);

        std::map<std::string, std::string> shortcutMap;

        shortcutMap["appid"] = std::to_string(entryID);
        shortcutMap["AppName"] = appName;
        shortcutMap["Exe"] = "\""+filepath+"\"";
        shortcutMap["StartDir"] = getDirectoryFromPath(filepath);
        shortcutMap["icon"] = iconPath;
        shortcutMap["ShortcutPath"] = "";
        shortcutMap["LaunchOptions"] = "";
        shortcutMap["IsHidden"] = "";
        shortcutMap["AllowDesktopConfig"] = "";
        shortcutMap["AllowOverlay"] = "";
        shortcutMap["OpenVR"] = "";
        shortcutMap["Devkit"] = "";
        shortcutMap["DevkitGameID"] = "";
        shortcutMap["DevkitOverrideAppID"] = "";
        shortcutMap["LastPlayTime"] = "+ne";
        shortcutMap["FlatpakAppID"] = "";

        return shortcutMap;
    }

    inline void updateShortcuts(std::vector<std::map<std::string, std::string>> shortcuts) {
        const std::string shortcutFile = getShortcutFile();

        std::string backupFile = getDirectoryFromPath(shortcutFile);
        backupFile.append("shortcuts.vdf.bak");
        copyFile(shortcutFile, backupFile);

        // Open the file for binary writing
        std::ofstream outFile(shortcutFile, std::ios::binary);

        // Check if the file is open
        if (outFile.is_open()) {
            std::vector<uint8_t> byteData;

            outFile.put(0x00);
            outFile << "shortcuts";
            outFile.put(0x00);

            int appId = 0;
            for (const std::map<std::string, std::string> shortcut : shortcuts) {
                outFile.put(0x00);
                outFile << std::to_string(appId);
                outFile.put(0x00).put(0x02);
                outFile << "appid";
                outFile.put(0x00);

                std::string exe = getValueFromMap(shortcut, "Exe");
                std::string appName = getValueFromMap(shortcut, "AppName");
                uint32_t shortcutId = generateShortcutId(exe, appName);
                outFile.write(reinterpret_cast<const char*>(&shortcutId), sizeof(shortcutId));

                writeShortcut(outFile, VDFStateMachine::FieldType::STRING, "AppName", getValueFromMap(shortcut, "AppName"));
                writeShortcut(outFile, VDFStateMachine::FieldType::STRING, "Exe", getValueFromMap(shortcut, "Exe"));
                writeShortcut(outFile, VDFStateMachine::FieldType::STRING, "StartDir", getValueFromMap(shortcut, "StartDir"));
                writeShortcut(outFile, VDFStateMachine::FieldType::STRING, "icon", getValueFromMap(shortcut, "icon"));
                writeShortcut(outFile, VDFStateMachine::FieldType::STRING, "ShortcutPath", getValueFromMap(shortcut, "ShortcutPath"));
                writeShortcut(outFile, VDFStateMachine::FieldType::STRING, "LaunchOptions", getValueFromMap(shortcut, "LaunchOptions"));
                writeShortcut(outFile, VDFStateMachine::FieldType::BOOLEAN, "IsHidden", getValueFromMap(shortcut, "IsHidden"));
                writeShortcut(outFile, VDFStateMachine::FieldType::BOOLEAN, "AllowDesktopConfig", getValueFromMap(shortcut, "AllowDesktopConfig"));
                writeShortcut(outFile, VDFStateMachine::FieldType::BOOLEAN, "AllowOverlay", getValueFromMap(shortcut, "AllowOverlay"));
                writeShortcut(outFile, VDFStateMachine::FieldType::BOOLEAN, "OpenVR", getValueFromMap(shortcut, "OpenVR"));
                writeShortcut(outFile, VDFStateMachine::FieldType::BOOLEAN, "Devkit", getValueFromMap(shortcut, "Devkit"));
                writeShortcut(outFile, VDFStateMachine::FieldType::STRING, "DevkitGameID", getValueFromMap(shortcut, "DevkitGameID"));
                writeShortcut(outFile, VDFStateMachine::FieldType::BOOLEAN, "DevkitOverrideAppID", getValueFromMap(shortcut, "DevkitOverrideAppID"));
                writeShortcut(outFile, VDFStateMachine::FieldType::DATE, "LastPlayTime", getValueFromMap(shortcut, "LastPlayTime"));
                writeShortcut(outFile, VDFStateMachine::FieldType::STRING, "FlatpakAppID", getValueFromMap(shortcut, "FlatpakAppID"));
                writeShortcut(outFile, VDFStateMachine::FieldType::LIST, "tags", getValueFromMap(shortcut, "tags"));
                appId++;
            }

            outFile.put(0x08).put(0x08);
            outFile.write(reinterpret_cast<char*>(byteData.data()), byteData.size());

            // Close the file
            outFile.close();

            std::cout << "File '" << shortcutFile << "' created successfully." << std::endl;
        } else {
            std::cerr << "Error opening file '" << shortcutFile << "' for writing." << std::endl;
        }
    }
}

#endif //VDFPARSER_H
