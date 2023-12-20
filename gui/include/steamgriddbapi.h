#ifndef STEAMGRIDDBAPI_H
#define STEAMGRIDDBAPI_H

#include <iostream>
#include <string>
#include <jsonutils.h>

namespace SteamGridDb {
    extern const std::map<std::string, std::string> gameIDs;
    extern std::string apiRoot;
    extern std::string apiKey;

    std::vector<std::string> getArtwork(ChiakiLog* log, std::string type, std::string queryParams, std::string gameId, int page);
    std::vector<std::string> getLandscapes(ChiakiLog* log, std::string gameId, int page);
    std::vector<std::string> getPortraits(ChiakiLog* log, std::string gameId, int page);
    std::vector<std::string> getHeroes(ChiakiLog* log, std::string gameId, int page);
    std::vector<std::string> getLogos(ChiakiLog* log, std::string gameId, int page);
    std::vector<std::string> getIcons(ChiakiLog* log, std::string gameId, int page);
}

#endif // STEAMGRIDDBAPI_H