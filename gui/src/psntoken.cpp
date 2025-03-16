#include "psntoken.h"
#include "psnaccountid.h"
#include <qjsonobject.h>
#include "jsonrequester.h"

#include <QObject>
#include <QDateTime>
#include <QDebug>

PSNToken::PSNToken(Settings *settings, QObject *parent)
    : QObject(parent)
    , settings(settings)
{
    basicAuthHeader = JsonRequester::generateBasicAuthHeader(PSNAuth::CLIENT_ID, PSNAuth::CLIENT_SECRET);
}

void PSNToken::InitPsnToken(QString redirectCode) {
    QString body = QString("grant_type=authorization_code&code=%1&scope=psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update&redirect_uri=https://remoteplay.dl.playstation.net/remoteplay/redirect&").arg(redirectCode);
    JsonRequester* requester = new JsonRequester(this);
    connect(requester, &JsonRequester::requestFinished, this, &PSNToken::handleAccessTokenResponse);
    connect(requester, &JsonRequester::requestError, this, &PSNToken::handleErrorResponse);
    requester->makePostRequest(PSNAuth::TOKEN_URL, basicAuthHeader, "application/x-www-form-urlencoded", std::move(body));
}

void PSNToken::RefreshPsnToken(QString refreshToken) {
    QString body = QString("grant_type=refresh_token&refresh_token=%1&scope=psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update&redirect_uri=https://remoteplay.dl.playstation.net/remoteplay/redirect&").arg(refreshToken);
    JsonRequester* requester = new JsonRequester(this);
    connect(requester, &JsonRequester::requestFinished, this, &PSNToken::handleAccessTokenResponse);
    connect(requester, &JsonRequester::requestError, this, &PSNToken::handleErrorResponse);
    requester->makePostRequest(PSNAuth::TOKEN_URL, basicAuthHeader, "application/x-www-form-urlencoded", std::move(body));
}

void PSNToken::handleAccessTokenResponse(const QString& url, const QJsonDocument& jsonDocument) {
    QJsonObject object = jsonDocument.object();
    QString access_token = object.value("access_token").toString();
    QString refresh_token = object.value("refresh_token").toString();
    QDateTime currentTime = QDateTime::currentDateTime();
    auto secondsLeft = object.value("expires_in").toInt();
    QDateTime expiry = currentTime.addSecs(secondsLeft);
    QString access_token_expiry = expiry.toString(settings->GetTimeFormat());
    settings->SetPsnAuthToken(std::move(access_token));
    settings->SetPsnRefreshToken(std::move(refresh_token));
    settings->SetPsnAuthTokenExpiry(std::move(access_token_expiry));
    emit PSNTokenSuccess();
    emit Finished();
}

void PSNToken::handleErrorResponse(const QString& url, const QString& error, const QNetworkReply::NetworkError& err) {
    emit PSNTokenError(QString("[E] : Url (%1) returned error (%2) with error code (%3)").arg(url).arg(error).arg(err));
    if(err == QNetworkReply::ProtocolInvalidOperationError)
        emit UnauthorizedError();
    emit Finished();
}