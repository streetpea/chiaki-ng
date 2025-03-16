#ifndef PSNTOKEN_H
#define PSNTOKEN_H

#include "settings.h"
#include "chiaki/log.h"

#include <ostream>
#include <QNetworkReply>
#include <qobject.h>
#include <sstream>

class PSNToken : public QObject {
    Q_OBJECT

public:
    PSNToken(Settings *settings, QObject* parent = nullptr);
    void InitPsnToken(QString redirectCode);
    void RefreshPsnToken(QString refreshToken);

signals:
    void PSNTokenSuccess();
    void PSNTokenError(const QString error);
    void UnauthorizedError();
    void Finished();

private:
    QString basicAuthHeader;
    Settings *settings = {};

private slots:
    void handleAccessTokenResponse(const QString& url, const QJsonDocument& jsonDocument);
    void handleErrorResponse(const QString& url, const QString& error, const QNetworkReply::NetworkError& err);
};

#endif //PSNTOKEN_H