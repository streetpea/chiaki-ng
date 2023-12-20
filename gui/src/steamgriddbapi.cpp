#include "steamgriddbapi.h"

#include <qjsonarray.h>

namespace SteamGridDb {
    const std::map<std::string, std::string> gameIDs {
                {"PS4 Remote Play", "5247907"},
                {"Chiaki Remote Play", "5319543"},
                {"Playstation 4", "5327254"},
                {"Playstation 5", "5327255"}
    };

    std::string apiRoot = "https://www.steamgriddb.com/api/v2";
    std::string apiKey = "112c9e0822e85e054b87793e684b231a"; //API Key for @Nikorag

    std::vector<std::string> getArtwork(ChiakiLog* log, std::string type, std::string queryParams, std::string gameId, int page) {
        std::string url;
        url.append(apiRoot);
        url.append("/");
        url.append(type);
        url.append("/game/");
        url.append(gameId);
        url.append("?page=");
        url.append(std::to_string(page));
        url.append(queryParams);

        std::vector<std::string> result_vector;
        QJsonDocument sgdbResult = JsonUtils::getResponseBodyBearer(log, url, apiKey);
        QJsonObject resultObject = sgdbResult.object();
        QJsonArray resultData = resultObject.value("data").toArray();


        for (QJsonArray::iterator it = resultData.begin(); it != resultData.end(); ++it) {
            QJsonObject gameObject = it->toObject();
            std::string imageUrl = gameObject.value("url").toString().toStdString();
            // Find the first occurrence of "\\/"
            size_t found = imageUrl.find("\\/");

            // Iterate and replace all occurrences of "\\/"
            while (found != std::string::npos) {
                imageUrl.replace(found, 2, "/"); // 2 is the length of "\\"
                found = imageUrl.find("\\/", found + 1);
            }
            result_vector.emplace_back(imageUrl);
        }

        return result_vector;
    }

    std::vector<std::string> getLandscapes(ChiakiLog* log, std::string gameId, int page) {
        return getArtwork(log, "grids", "&dimensions=460x215,920x430", gameId, page);
    }

    std::vector<std::string> getPortraits(ChiakiLog* log, std::string gameId, int page) {
        return getArtwork(log, "grids", "&dimensions=600x900", gameId, page);
    }

    std::vector<std::string> getHeroes(ChiakiLog* log, std::string gameId, int page) {
        return getArtwork(log, "heroes", "", gameId, page);
    }

    std::vector<std::string> getLogos(ChiakiLog* log, std::string gameId, int page) {
        return getArtwork(log, "logos", "", gameId, page);
    }

    std::vector<std::string> getIcons(ChiakiLog* log, std::string gameId, int page) {
        return getArtwork(log, "icons", "", gameId, page);
    }
}
