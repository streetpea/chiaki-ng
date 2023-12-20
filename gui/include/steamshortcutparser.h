#ifndef VDFPARSER_H
#define VDFPARSER_H

#include <fstream>
#include <string>
#include <vdfstatemachine.h>

#include <cstdio>

#include "mainwindow.h"
#include "chiaki/log.h"
#if defined(_WIN32)
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#else
#endif


namespace SteamShortcutParser {
    extern std::vector<char> fileHeader;
    extern std::string controller_layout_workshop_id;
    // Callback function to write data to a file
    size_t WriteCallback(void* contents, size_t size, size_t nmemb, FILE* stream);
    void createDirectories(ChiakiLog* log, const std::string& path);
    void copyFile(ChiakiLog* log, const std::string& sourcePath, const std::string& destinationPath);
    // Function to download a file from a URL to a destination directory
    bool downloadFile(ChiakiLog* log, const char* url, const char* destPath);
    void downloadAssets(ChiakiLog* log, std::string gridPath, std::string shortAppId, std::map<std::string, std::string> artwork);
    uint64_t generatePreliminaryId(const std::string& exe, const std::string& appname);
    // Function to generate App ID
    std::string generateAppId(const std::string& exe, const std::string& appname);
    // Function to shorten App ID
    std::string shortenAppId(const std::string& longId);
    // Function to generate Short App ID
    std::string generateShortAppId(const std::string& exe, const std::string& appname);
    uint32_t generateShortcutId(const std::string& exe, const std::string& appname);
    std::string getValueFromMap(const std::map<std::string, std::string>& myMap, const std::string& key);
    bool containsString(const std::vector<std::string>& stringArray, const std::string& targetString);
    bool compareFirstElements(int size, const std::vector<char>& vector1, const std::vector<char>& vector2);
    std::vector<std::string> splitString(std::string input, char delimiter);
    int countStringOccurrencesInFile(ChiakiLog* log, const std::string& filename, const std::string& targetString);
    std::string getDirectoryFromPath(std::string fullPath);
    std::string getSteamBaseDir();
    bool steamExists();
    std::string getMostRecentUser(ChiakiLog* log);
    std::string getShortcutFile(ChiakiLog* log);
    std::vector<std::map<std::string, std::string>> parseShortcuts(ChiakiLog* log);
    void writeShortcut(std::ofstream& outFile, VDFStateMachine::FieldType type, const std::string key, const std::string value);
    std::map<std::string, std::string> buildShortcutEntry(ChiakiLog* log, const DisplayServer* server, std::string filepath, std::map<std::string, std::string> artwork);
    void updateShortcuts(ChiakiLog* log, std::vector<std::map<std::string, std::string>> shortcuts);
    void updateControllerConfig(ChiakiLog* log, std::string titleName);
}

#endif //VDFPARSER_H
