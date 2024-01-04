#include "steamgriddbapi.h"

#include <qjsonarray.h>
#include <qjsonobject.h>

#include "jsonrequester.h"

SteamGridDb::SteamGridDb(QObject* parent) {
    apiRoot = "https://www.steamgriddb.com/api/v2";
    apiKey = "112c9e0822e85e054b87793e684b231a"; //API Key for @Nikorag
}

void SteamGridDb::getArtwork(ChiakiLog* log, QString gameId, ArtworkType type, int page) {
    switch(type) {
        case ArtworkType::LANDSCAPE:
            getLandscapes(log, gameId, page);
            break;
        case ArtworkType::PORTRAIT:
            getPortraits(log, gameId, page);
            break;
        case ArtworkType::HERO:
            getHeroes(log, gameId, page);
            break;
        case ArtworkType::ICON:
            getIcons(log, gameId, page);
            break;
        case ArtworkType::LOGO:
            getLogos(log, gameId, page);
            break;
    }
}

void SteamGridDb::getLandscapes(ChiakiLog* log, QString gameId, int page) {
    requestArtwork(log, "grids", "&dimensions=460x215,920x430", gameId, page);
}

void SteamGridDb::getPortraits(ChiakiLog* log, QString gameId, int page) {
    requestArtwork(log, "grids", "&dimensions=600x900", gameId, page);
}

void SteamGridDb::getHeroes(ChiakiLog* log, QString gameId, int page) {
    requestArtwork(log, "heroes", "", gameId, page);
}

void SteamGridDb::getLogos(ChiakiLog* log, QString gameId, int page) {
    requestArtwork(log, "logos", "", gameId, page);
}

void SteamGridDb::getIcons(ChiakiLog* log, QString gameId, int page) {
    requestArtwork(log, "icons", "", gameId, page);
}

void SteamGridDb::requestArtwork(ChiakiLog* log, QString type, QString queryParams, QString gameId, int page) {
    QString url = QString("%1/%2/game/%3?page=%4%5")
        .arg(apiRoot)
        .arg(type)
        .arg(gameId)
        .arg(QString::number(page))
        .arg(queryParams);

    QString bearerToken = JsonRequester::generateBearerAuthHeader(apiKey);
    JsonRequester* requester = new JsonRequester(this);
    connect(requester, &JsonRequester::requestFinished, this, &SteamGridDb::handleJsonResponse);
    requester->makeGetRequest(url, bearerToken);
}

void SteamGridDb::handleJsonResponse(const QString& url, const QJsonDocument jsonDocument) {
    QVector<QString> result_vector;
    QJsonObject resultObject = jsonDocument.object();
    QJsonArray resultData = resultObject.value("data").toArray();


    for (QJsonArray::iterator it = resultData.begin(); it != resultData.end(); ++it) {
        QJsonObject gameObject = it->toObject();
        QString imageUrl = gameObject.value("url").toString();

        result_vector.append(imageUrl);
    }

    emit handleArtworkResponse(result_vector);
}
