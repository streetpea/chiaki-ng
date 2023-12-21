#include "psnaccountid.h"

#include <qjsonobject.h>
#include <QObject>
#include "jsonrequester.h"

PSNAccountID::PSNAccountID(QObject* parent) {
    basicAuthHeader = JsonRequester::generateBasicAuthHeader(PSNAuth::CLIENT_ID, PSNAuth::CLIENT_SECRET);
}

void PSNAccountID::GetPsnAccountId(QString redirectCode) {
    QString body = QString("grant_type=authorization_code&code=%1&redirect_uri=https://remoteplay.dl.playstation.net/remoteplay/redirect&").arg(redirectCode);
    JsonRequester* requester = new JsonRequester(this);
    connect(requester, &JsonRequester::requestFinished, this, &PSNAccountID::handleAccessTokenResponse);
    requester->makePostRequest(PSNAuth::TOKEN_URL, basicAuthHeader, "application/x-www-form-urlencoded", body);
}

void PSNAccountID::handleAccessTokenResponse(const QString& url, const QJsonDocument& jsonDocument) {
    QJsonObject object = jsonDocument.object();
    QString access_token = object.value("access_token").toString();

    QString accountInfoUrl = QString("%1/%2").arg(PSNAuth::TOKEN_URL).arg(access_token);
    JsonRequester* requester = new JsonRequester(this);
    connect(requester, &JsonRequester::requestFinished, this, &PSNAccountID::handUserIDResponse);
    requester->makeGetRequest(accountInfoUrl, basicAuthHeader, "application/json");
}

void PSNAccountID::handUserIDResponse(const QString& url, const QJsonDocument& jsonDocument) {
    QJsonObject object = jsonDocument.object();
    QString user_id = object.value("user_id").toString();

    QByteArray byte_representation = to_bytes(std::stoll(user_id.toStdString()), 8);
    emit AccountIDResponse(byte_representation.toBase64().toStdString());
}
