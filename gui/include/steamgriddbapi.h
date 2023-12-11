#ifndef STEAMGRIDDBAPI_H
#define STEAMGRIDDBAPI_H

#include <iostream>
#include <string>
#include <curl/curl.h>
#include <vdfstatemachine.h>

namespace SteamGridDb {
    static const std::map<std::string, std::string> gameIDs {
        {"PS4 Remote Play", "5247907"},
        {"Chiaki Remote Play", "5319543"},
        {"Playstation 4", "5327254"},
        {"Playstation 5", "5327255"}
    };
    static std::string apiRoot = "https://www.steamgriddb.com/api/v2";
    static std::string apiKey = "112c9e0822e85e054b87793e684b231a"; //API Key for @Nikorag

    // Callback function to write the response data to a string
    inline size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
        size_t totalSize = size * nmemb;
        output->append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }

    // Function to make an HTTP GET request with a Bearer token and return the response as a string
    inline std::string makeHttpRequest(const std::string& url, const std::string& bearerToken) {
        // Initialize libcurl
        CURL* curl = curl_easy_init();
        if (!curl) {
            std::cerr << "Error initializing libcurl." << std::endl;
            return "";
        }

        // Set the URL for the request
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        // Set the callback function to write the response to a string
        std::string response;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // Set the Bearer token in the request header
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("Authorization: Bearer " + bearerToken).c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Perform the request
        CURLcode res = curl_easy_perform(curl);

        // Check for errors
        if (res != CURLE_OK) {
            std::cerr << "Error during HTTP request: " << curl_easy_strerror(res) << std::endl;
            response = "";  // Clear the response in case of error
        }

        // Clean up
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return response;
    }

    inline std::vector<std::string> getArtwork(std::string type, std::string queryParams, std::string gameId, int page) {
        std::string url;
        url.append(apiRoot);
        url.append("/");
        url.append(type);
        url.append("/game/");
        url.append(gameId);
        url.append("?page=");
        url.append(std::to_string(page));
        url.append(queryParams);

        std::string grids = makeHttpRequest(url, apiKey);

        // Your regular expression pattern with groups
        std::regex regex_pattern("\"url\":\"([^\"]+)\"");

        // Iterator for matching
        std::sregex_iterator iter(grids.begin(), grids.end(), regex_pattern);
        std::sregex_iterator end;

        // Vector to store group 1 of each match
        std::vector<std::string> result_vector;

        // Iterate over matches and store group 1 in the vector
        while (iter != end) {
            std::string imageUrl = (*iter)[1].str();

            // Find the first occurrence of "\\/"
            size_t found = imageUrl.find("\\/");

            // Iterate and replace all occurrences of "\\/"
            while (found != std::string::npos) {
                imageUrl.replace(found, 2, "/"); // 2 is the length of "\\"
                found = imageUrl.find("\\/", found + 1);
            }

            result_vector.emplace_back(imageUrl);
            ++iter;
        }

        return result_vector;
    }

    inline std::vector<std::string> getLandscapes(std::string gameId,int page) {
        return getArtwork("grids", "&dimensions=460x215,920x430", gameId, page);
    }

    inline std::vector<std::string> getPortraits(std::string gameId,int page) {
        return getArtwork("grids", "&dimensions=600x900", gameId, page);
    }

    inline std::vector<std::string> getHeroes(std::string gameId,int page) {
        return getArtwork("heroes", "", gameId, page);
    }

    inline std::vector<std::string> getLogos(std::string gameId,int page) {
        return getArtwork("logos", "", gameId, page);
    }

    inline std::vector<std::string> getIcons(std::string gameId,int page) {
        return getArtwork("icons", "", gameId, page);
    }
}

#endif //STEAMGRIDDBAPI_H
