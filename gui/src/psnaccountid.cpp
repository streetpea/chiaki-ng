#include "psnaccountid.h"
#include "jsonrequester.h"

#include <qjsonobject.h>
#include <QObject>
#include <QDebug>
PSNAccountID::PSNAccountID(Settings *settings, QObject *parent)
    : QObject(parent)
    , settings(settings)
{
    basicAuthHeader = JsonRequester::generateBasicAuthHeader(PSNAuth::CLIENT_ID, PSNAuth::CLIENT_SECRET);
}

void PSNAccountID::GetPsnAccountId(QString redirectCode) {
    QString body = QString("grant_type=authorization_code&code=%1&scope=psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update&redirect_uri=https://remoteplay.dl.playstation.net/remoteplay/redirect&").arg(redirectCode);
    JsonRequester* requester = new JsonRequester(this);
    connect(requester, &JsonRequester::requestFinished, this, &PSNAccountID::handleAccessTokenResponse);
    connect(requester, &JsonRequester::requestError, this, &PSNAccountID::handleErrorResponse);
    requester->makePostRequest(PSNAuth::TOKEN_URL, basicAuthHeader, "application/x-www-form-urlencoded", std::move(body));
}

void PSNAccountID::handleAccessTokenResponse(const QString& url, const QJsonDocument& jsonDocument) {
    QJsonObject object = jsonDocument.object();
    QString access_token = object.value("access_token").toString();
    QString refresh_token = object.value("refresh_token").toString();
    QDateTime currentTime = QDateTime::currentDateTime();
    auto secondsLeft = object.value("expires_in").toInt();
    QDateTime expiry = currentTime.addSecs(secondsLeft);
    QString access_token_expiry = expiry.toString(settings->GetTimeFormat());
    settings->SetPsnAuthToken(access_token);
    settings->SetPsnRefreshToken(std::move(refresh_token));
    settings->SetPsnAuthTokenExpiry(std::move(access_token_expiry));

    QString accountInfoUrl = QString("%1/%2").arg(PSNAuth::TOKEN_URL).arg(access_token);
    JsonRequester* requester = new JsonRequester(this);
    connect(requester, &JsonRequester::requestFinished, this, &PSNAccountID::handUserIDResponse);
    connect(requester, &JsonRequester::requestError, this, &PSNAccountID::handleErrorResponse);
    requester->makeGetRequest(accountInfoUrl, basicAuthHeader, "application/json");
}

void PSNAccountID::handUserIDResponse(const QString& url, const QJsonDocument& jsonDocument) {
    QJsonObject object = jsonDocument.object();
    QString user_id = object.value("user_id").toString();
    QByteArray byte_representation = to_bytes_little_endian(std::stoll(user_id.toStdString()), 8);
    settings->SetPsnAccountId(byte_representation.toBase64());
    emit AccountIDResponse(byte_representation.toBase64());
    emit Finished();
}

void PSNAccountID::handleErrorResponse(const QString& url, const QString& error, const QNetworkReply::NetworkError& err) {
    emit AccountIDError(url, error);
    emit Finished();
}
