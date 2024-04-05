#include "psntoken.h"
#include "psnaccountid.h"
#include <qjsonobject.h>
#include "jsonrequester.h"

#include <QObject>
#include <QDateTime>

PSNToken::PSNToken(Settings *settings, QObject *parent)
    : QObject(parent)
    , settings(settings)
{
    basicAuthHeader = JsonRequester::generateBasicAuthHeader(PSNAuth::CLIENT_ID, PSNAuth::CLIENT_SECRET);
}

void PSNToken::InitPsnToken(QString redirectCode) {
    QString body = QString("grant_type=authorization_code&code=%1&redirect_uri=https://remoteplay.dl.playstation.net/remoteplay/redirect&").arg(redirectCode);
    JsonRequester* requester = new JsonRequester(this);
    connect(requester, &JsonRequester::requestFinished, this, &PSNToken::handleAccessTokenResponse);
    connect(requester, &JsonRequester::requestError, this, &PSNToken::handleErrorResponse);
    requester->makePostRequest(PSNAuth::TOKEN_URL, basicAuthHeader, "application/x-www-form-urlencoded", body);
}

void PSNToken::RefreshPsnToken() {
    QString body = QString("grant_type=refresh_token&refresh_token=%1&redirect_uri=https://remoteplay.dl.playstation.net/remoteplay/redirect&").arg(settings->GetPsnRefreshToken());
    JsonRequester* requester = new JsonRequester(this);
    connect(requester, &JsonRequester::requestFinished, this, &PSNToken::handleAccessTokenResponse);
    connect(requester, &JsonRequester::requestError, this, &PSNToken::handleErrorResponse);
    requester->makePostRequest(PSNAuth::TOKEN_URL, basicAuthHeader, "application/x-www-form-urlencoded", body);
}

void PSNToken::handleAccessTokenResponse(const QString& url, const QJsonDocument& jsonDocument) {
    QJsonObject object = jsonDocument.object();
    QString access_token = object.value("access_token").toString();
    QString refresh_token = object.value("refresh_token").toString();
    QDateTime expiry = QDateTime::currentDateTime();
    QString access_token_expiry = expiry.addSecs(object.value("expires_in").toString().toInt()).toString(settings->GetTimeFormat());
    settings->SetPsnAuthToken(access_token);
    settings->SetPsnRefreshToken(refresh_token);
    settings->SetPsnAuthTokenExpiry(access_token_expiry);
    emit PSNTokenSuccess();
}

void PSNToken::handleErrorResponse(const QString& url, const QString& error) {
    emit PSNTokenError(QString("[E] : Url (%1) returned error (%2)").arg(url).arg(error));
}