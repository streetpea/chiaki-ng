#ifndef STEAMGRIDDBAPI_H
#define STEAMGRIDDBAPI_H

#include <iostream>
#include <map>
#include <QJsonDocument>
#include <QObject>
#include <string>

#include "sgdbenums.h"
#include "chiaki/log.h"

class SteamGridDb : public QObject {
    Q_OBJECT

    public:
        QString apiRoot;
        QString apiKey;
        explicit SteamGridDb(QObject* parent = nullptr);
        void getArtwork(ChiakiLog* log, QString gameId, ArtworkType type, int page);

    private:
        void requestArtwork(ChiakiLog* log, QString type, QString queryParams, QString gameId, int page);
        void getLandscapes(ChiakiLog* log, QString gameId, int page);
        void getPortraits(ChiakiLog* log, QString gameId, int page);
        void getHeroes(ChiakiLog* log, QString gameId, int page);
        void getLogos(ChiakiLog* log, QString gameId, int page);
        void getIcons(ChiakiLog* log, QString gameId, int page);

    public slots:
        void handleJsonResponse(const QString& url, const QJsonDocument jsonDocument);

    signals:
        void handleArtworkResponse(QVector<QString> artwork);
};

namespace SteamGridDbConstants {
    const QMap<QString, QString> gameIDs {
                            {"PS4 Remote Play", "5247907"},
                            {"Chiaki Remote Play", "5319543"},
                            {"Playstation 4", "5327254"},
                            {"Playstation 5", "5327255"}
    };
};

#endif // STEAMGRIDDBAPI_H