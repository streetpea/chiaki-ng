//
// Based on script by grill2010
//

#ifndef PSNACCOUNTID_H
#define PSNACCOUNTID_H

#include "settings.h"

#include <ostream>
#include <QNetworkReply>
#include <qobject.h>
#include <sstream>

#include "chiaki/log.h"

namespace PSNAuth {
    static QString CLIENT_ID = "ba495a24-818c-472b-b12d-ff231c1b5745";
    static QString CLIENT_SECRET = "mvaiZkRsAsI1IBkY";
    static const QString LOGIN_URL = QString(
                "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize?service_entity=urn:service-entity:psn&response_type=code&client_id=%1&redirect_uri=https://remoteplay.dl.playstation.net/remoteplay/redirect&scope=psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update&request_locale=en_US&ui=pr&service_logo=ps&layout_type=popup&smcid=remoteplay&prompt=always&PlatformPrivacyWs1=minimal&")
            .arg(CLIENT_ID);
    static const QString TOKEN_URL = "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token";
    static const std::string REDIRECT_PAGE = "https://remoteplay.dl.playstation.net/remoteplay/redirect";
}

class PSNAccountID : public QObject {
    Q_OBJECT

public:
    PSNAccountID(Settings *settings, QObject *parent = nullptr);


    void GetPsnAccountId(QString redirectCode);


signals:
    void AccountIDResponse(QString accountId);
    void AccountIDError(const QString& url, const QString& error);
    void Finished();

private:
    QString basicAuthHeader;
    Settings *settings = {};

    static QByteArray to_bytes_little_endian(long long number, int num_bytes) {
        QByteArray byte_array;
        int n = 1;
        if(*(char *)&n == 1) // little endian
        {
            for (int i = 0; i < num_bytes; i++)
            {
                char result = number & 0xFF;
                byte_array.append(result);
                number >>= 8;
            }
        }
        else // big endian
        {
            for (int i = num_bytes - 1; i >= 0; i--)
            {
                char result = (number >> (8 * num_bytes)) & 0xFF;
                byte_array.append(result);
            }    
        }
        return byte_array;
    }

private slots:
    void handleAccessTokenResponse(const QString& url, const QJsonDocument& jsonDocument);

    void handUserIDResponse(const QString& url, const QJsonDocument& jsonDocument);

    void handleErrorResponse(const QString& url, const QString& error, const QNetworkReply::NetworkError& err);
};

#endif //PSNACCOUNTID_H
