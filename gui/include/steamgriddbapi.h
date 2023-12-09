//
// Created by Jamie Bartlett on 09/12/2023.
//

#ifndef STEAMGRIDDBAPI_H
#define STEAMGRIDDBAPI_H

#include <iostream>
#include <string>
#include <curl/curl.h>
#include <vdfstatemachine.h>

namespace SteamGridDb {
    static std::vector<std::string> gameIDs = {"5247907"};
    static std::string apiRoot = "https://www.steamgriddb.com/api/v2";
    static std::string apiKey = "f80f92019254471cca9d62ff91c21eee";

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

    inline std::vector<std::string> getArtwork(std::string type, std::string queryParams, int page) {
        std::string url;
        url.append(apiRoot);
        url.append("/");
        url.append(type);
        url.append("/game/");
        url.append(VDFStateMachine::VALUE::delimit(gameIDs, ','));
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

    inline std::vector<std::string> getLandscapes(int page) {
        return getArtwork("grids", "&dimensions=460x215,920x430", page);
    }

    inline std::vector<std::string> getPortraits(int page) {
        return getArtwork("grids", "&dimensions=600x900", page);
    }

    inline std::vector<std::string> getHeroes(int page) {
        return getArtwork("heroes", "", page);
    }

    inline std::vector<std::string> getLogos(int page) {
        return getArtwork("logos", "", page);
    }

    inline std::vector<std::string> getIcons(int page) {
        return getArtwork("icons", "", page);
    }
}

#endif //STEAMGRIDDBAPI_H
