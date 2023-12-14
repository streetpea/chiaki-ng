#ifndef STEAMGRIDDBAPI_H
#define STEAMGRIDDBAPI_H

#include <iostream>
#include <string>
#include <vdfstatemachine.h>
#include <jsonutils.h>

namespace SteamGridDb {
    static const std::map<std::string, std::string> gameIDs {
        {"PS4 Remote Play", "5247907"},
        {"Chiaki Remote Play", "5319543"},
        {"Playstation 4", "5327254"},
        {"Playstation 5", "5327255"}
    };
    static std::string apiRoot = "https://www.steamgriddb.com/api/v2";
    static std::string apiKey = "112c9e0822e85e054b87793e684b231a"; //API Key for @Nikorag

    inline std::vector<std::string> getArtwork(ChiakiLog* log, std::string type, std::string queryParams, std::string gameId, int page) {
        std::string url;
        url.append(apiRoot);
        url.append("/");
        url.append(type);
        url.append("/game/");
        url.append(gameId);
        url.append("?page=");
        url.append(std::to_string(page));
        url.append(queryParams);

        std::vector<std::string> result_vector = JsonUtils::getJsonAttributesBearer(log, url, apiKey, "url");

        for (std::string& imageUrl : result_vector) {
            // Find the first occurrence of "\\/"
            size_t found = imageUrl.find("\\/");

            // Iterate and replace all occurrences of "\\/"
            while (found != std::string::npos) {
                imageUrl.replace(found, 2, "/"); // 2 is the length of "\\"
                found = imageUrl.find("\\/", found + 1);
            }
        }

        return result_vector;
    }

    inline std::vector<std::string> getLandscapes(ChiakiLog* log, std::string gameId,int page) {
        return getArtwork(log, "grids", "&dimensions=460x215,920x430", gameId, page);
    }

    inline std::vector<std::string> getPortraits(ChiakiLog* log, std::string gameId,int page) {
        return getArtwork(log, "grids", "&dimensions=600x900", gameId, page);
    }

    inline std::vector<std::string> getHeroes(ChiakiLog* log, std::string gameId,int page) {
        return getArtwork(log, "heroes", "", gameId, page);
    }

    inline std::vector<std::string> getLogos(ChiakiLog* log, std::string gameId,int page) {
        return getArtwork(log, "logos", "", gameId, page);
    }

    inline std::vector<std::string> getIcons(ChiakiLog* log, std::string gameId,int page) {
        return getArtwork(log, "icons", "", gameId, page);
    }
}

#endif //STEAMGRIDDBAPI_H
