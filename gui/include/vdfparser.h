//
// Created by Jamie Bartlett on 07/12/2023.
//

#ifndef VDFPARSER_H
#define VDFPARSER_H
#include <fstream>
#include <string>
#include <crc.h>

namespace VDFParser {

    enum class FieldType {
        STRING,
        BOOLEAN,
        LIST,
        DATE
    };

    enum class ParseState {
        APPID,
        WAITING,
        KEY,
        VALUE,
        ENDING
    };

    enum class ListParseState {
        WAITING,
        INDEX,
        VALUE,
        ENDING
    };

    static std::vector<std::string> headers = {
        "appid",
        "AppName",
        "Exe",
        "StartDir",
        "icon",
        "ShortcutPath",
        "LaunchOptions",
        "IsHidden",
        "AllowDesktopConfig",
        "AllowOverlay",
        "OpenVR",
        "Devkit",
        "DevkitGameID",
        "DevkitOverrideAppID",
        "LastPlayTime",
        "FlatpakAppID",
        "tags"
    };

    static std::vector<char> fileHeader = { 0x00, 0x73, 0x68, 0x6F, 0x72, 0x74, 0x63, 0x75, 0x74, 0x73, 0x00 };

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

    inline std::string delimit(const std::vector<std::string>& vec, char delimiter) {
        std::stringstream result;

        // Iterate through the vector
        for (auto it = vec.begin(); it != vec.end(); ++it) {
            // Append the current string to the result
            result << *it;

            // Add a comma if it's not the last element
            if (std::next(it) != vec.end()) {
                result << delimiter;
            }
        }

        // Convert the stringstream to a string and return
        return result.str();
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
        // Split the path by "/"
        std::vector<std::string> parts;
        std::istringstream iss(fullPath);
        std::string part;
        while (std::getline(iss, part, '/')) {
            parts.push_back(part);
        }

        // Rejoin all parts except the last one
        std::ostringstream os;
        for (size_t i = 0; i < parts.size() - 1; ++i) {
            os << parts[i];
            if (i < parts.size() - 2) {
                os << '/';
            }
        }

        return os.str();
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

            ParseState state = ParseState::APPID;
            ListParseState listState = ListParseState::WAITING;
            FieldType type;
            std::ostringstream utf8String;
            std::string key;
            std::map<std::string, std::string> entry;
            std::vector<char> endingBuffer;
            std::vector<std::string> listValue;

            for (char byte : buffer) {
                // Convert the byte to an unsigned integer (uint8)
                uint8_t value = static_cast<uint8_t>(byte);

                //Print the file
                if (value == 0x00) {
                    std::cout << "\\0x00";
                } else if (value == 0x01) {
                    std::cout << "\\0x01";
                } else if (value == 0x02) {
                    std::cout << "\\0x02";
                } else if (value == 0x08) {
                    std::cout << "\\0x08";
                } else {
                    std::cout << static_cast<char>(value);
                }

                //Parse it
                if (state == ParseState::APPID && value == 0x01) {
                    //Update the state
                    state = ParseState::KEY;
                    if (value == 0x01) type = FieldType::STRING;
                } else if (state == ParseState::WAITING && value != 0x08) {
                    //Update the state
                    state = ParseState::KEY;
                    if (value == 0x01) type = FieldType::STRING;
                    if (value == 0x02) type = FieldType::BOOLEAN;
                    if (value == 0x00) {
                        listValue.clear();
                        type = FieldType::LIST;
                    }
                } else if (state == ParseState::KEY && value != 0x00) {
                    utf8String << static_cast<char>(value);
                } else if (state == ParseState::KEY) {
                    //Set key and reset string
                    key = utf8String.str();
                    utf8String.str("");

                    //Update the state
                    state = ParseState::VALUE;
                    if (key == "LastPlayTime") type = FieldType::DATE;

                //Handle Strings
                } else if (state == ParseState::VALUE && type == FieldType::STRING && value != 0x00) {
                    utf8String << static_cast<char>(value);
                } else if (state == ParseState::VALUE && type == FieldType::STRING) {
                    entry[key] = utf8String.str();
                    utf8String.str("");

                    //Update the state
                    state = ParseState::WAITING;
                //Handle Booleans and dates
                } else if (state == ParseState::VALUE && type == FieldType::BOOLEAN) {
                    if (value == 0x01) entry[key] = "true";
                    if (value == 0x00) entry[key] = "false";

                    //Update the state
                    state = ParseState::ENDING;
                }  else if (state == ParseState::VALUE && type == FieldType::DATE) {
                    entry[key] = "";

                    //Update the state
                    state = ParseState::ENDING;
                } else if (state == ParseState::ENDING && (type == FieldType::BOOLEAN || type == FieldType::DATE) && endingBuffer.size() < 2) {
                    endingBuffer.emplace_back(value);
                } else if (state == ParseState::ENDING && (type == FieldType::BOOLEAN || type == FieldType::DATE) && endingBuffer.size() == 2) {
                    endingBuffer.clear();
                    //Update the state
                    state = ParseState::WAITING;
                //Handle Lists
                } else if (state == ParseState::VALUE && type == FieldType::LIST) {
                    if (listState == ListParseState::WAITING && value == 0x08 && endingBuffer.size() < 1) {
                        endingBuffer.emplace_back(value);
                    } else if (listState == ListParseState::WAITING && value == 0x08 && endingBuffer.size() == 1) {
                        entry[key] = delimit(listValue, ',');
                        endingBuffer.clear();
                        //Update states
                        // Bit of a hack here but we know that tags is the only list and it's last off the block so we handle that here
                        listState = ListParseState::WAITING;
                        state = ParseState::APPID;
                        //Add this entry and make a new one
                        shortcuts.emplace_back(entry);
                        entry.clear();
                    } else if (listState == ListParseState::WAITING && value != 0x08) {
                        listState = ListParseState::INDEX;
                    } else if (listState == ListParseState::INDEX && value == 0x00) {
                        listState = ListParseState::VALUE;
                        utf8String.str("");
                    } else if (listState == ListParseState::VALUE && value != 0x00) {
                        utf8String << static_cast<char>(value);
                    } else if (listState == ListParseState::VALUE) {
                        listValue.emplace_back(utf8String.str());
                        listState = ListParseState::WAITING;
                        utf8String.str("");
                    }
                }
            }

            std::cout << std::endl;

        } else {
            std::cerr << "Error opening file: " << shortcutFile << std::endl;
        }

        for (const auto& myMap : shortcuts) {
            std::cout << "{ ";

            for (auto it = myMap.begin(); it != myMap.end(); ++it) {
                std::cout << it->first << ": " << it->second;

                // Add a comma if it's not the last element
                if (std::next(it) != myMap.end()) {
                    std::cout << ", ";
                }
            }

            std::cout << " }" << std::endl;
        }

        return shortcuts;
    }

    inline void writeShortcut(std::ofstream& outFile, FieldType type, const std::string key, const std::string value) {
        // Write the identifying bit
        if (type == FieldType::LIST) {
            outFile.put(0x00);
        } else if (type == FieldType::STRING) {
            outFile.put(0x01);
        } else {
            outFile.put(0x02);
        }

        // Write the key
        outFile << key;

        // Write the divider
        outFile.put(0x00);

        // Write the value based on the type
        if (type == FieldType::STRING) {
            outFile << value;
            // Ending sequence for string
            outFile.put(0x00);
        } else if (type == FieldType::BOOLEAN) {
            // Boolean value encoding (0x01 for true, 0x00 for false)
            outFile.put(value == "true" ? 0x01 : 0x00);
            // Ending sequence for boolean
            outFile.put(0x00).put(0x00).put(0x00);
        } else if (type == FieldType::DATE) {
            // Date encoding (0x00 0x00 0x00 0x00)
            outFile.put(0x00).put(0x00).put(0x00).put(0x00);
        } else if (type == FieldType::LIST) {
            std::vector<std::string> list = VDFParser::splitString(value, ',');
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

    inline std::map<std::string, std::string> buildShortcutEntry(const DisplayServer* server, std::string filepath) {
        std::string shortcutFile = VDFParser::getShortcutFile();

        int entryID = countStringOccurrencesInFile(shortcutFile, "appid");
        entryID++;

        std::map<std::string, std::string> shortcutMap;

        shortcutMap["appid"] = std::to_string(entryID);
        shortcutMap["AppName"] = server->registered_host.GetServerNickname().toStdString();
        shortcutMap["Exe"] = "\""+filepath+"\"";
        shortcutMap["StartDir"] = "\""+getDirectoryFromPath(filepath)+"/\"";
        shortcutMap["icon"] = "";
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

                writeShortcut(outFile, FieldType::STRING, "AppName", getValueFromMap(shortcut, "AppName"));
                writeShortcut(outFile, FieldType::STRING, "Exe", getValueFromMap(shortcut, "Exe"));
                writeShortcut(outFile, FieldType::STRING, "StartDir", getValueFromMap(shortcut, "StartDir"));
                writeShortcut(outFile, FieldType::STRING, "icon", getValueFromMap(shortcut, "icon"));
                writeShortcut(outFile, FieldType::STRING, "ShortcutPath", getValueFromMap(shortcut, "ShortcutPath"));
                writeShortcut(outFile, FieldType::STRING, "LaunchOptions", getValueFromMap(shortcut, "LaunchOptions"));
                writeShortcut(outFile, FieldType::BOOLEAN, "IsHidden", getValueFromMap(shortcut, "IsHidden"));
                writeShortcut(outFile, FieldType::BOOLEAN, "AllowDesktopConfig", getValueFromMap(shortcut, "AllowDesktopConfig"));
                writeShortcut(outFile, FieldType::BOOLEAN, "AllowOverlay", getValueFromMap(shortcut, "AllowOverlay"));
                writeShortcut(outFile, FieldType::BOOLEAN, "OpenVR", getValueFromMap(shortcut, "OpenVR"));
                writeShortcut(outFile, FieldType::BOOLEAN, "Devkit", getValueFromMap(shortcut, "Devkit"));
                writeShortcut(outFile, FieldType::STRING, "DevkitGameID", getValueFromMap(shortcut, "DevkitGameID"));
                writeShortcut(outFile, FieldType::BOOLEAN, "DevkitOverrideAppID", getValueFromMap(shortcut, "DevkitOverrideAppID"));
                writeShortcut(outFile, FieldType::DATE, "LastPlayTime", getValueFromMap(shortcut, "LastPlayTime"));
                writeShortcut(outFile, FieldType::STRING, "FlatpakAppID", getValueFromMap(shortcut, "FlatpakAppID"));
                writeShortcut(outFile, FieldType::LIST, "tags", getValueFromMap(shortcut, "tags"));
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
