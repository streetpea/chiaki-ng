#ifndef JSONREQUESTER_H
#define JSONREQUESTER_H
#include <QObject>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class JsonRequester : public QObject {
    Q_OBJECT

public:
    explicit JsonRequester(QObject* parent = nullptr);

    void makePostRequest(const QString& url, const QString& authHeader, QString contentType = "application/json",
    QString body = "");

    void makeGetRequest(const QString& url, const QString& authHeader, QString contentType = "application/json");

    static QString generateBearerAuthHeader(QString bearerToken);

    static QString generateBasicAuthHeader(QString username, QString password);


signals:
    void requestFinished(const QString& url, const QJsonDocument& jsonDocument);
    void requestError(const QString& url, const QString& error, const QNetworkReply::NetworkError& err);

private slots:
    void onRequestFinished(QNetworkReply* reply);

private:
    void makeRequest(bool post, const QString& url, const QString& authHeader, QString contentType,
                     QString body = nullptr);

    QNetworkAccessManager* networkManager;
    QHash<QNetworkReply *, QString> currentReplies;
};

#endif //JSONREQUESTER_H
