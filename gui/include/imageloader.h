#ifndef IMAGELOADER_H
#define IMAGELOADER_H
#include <QDialog>
#include <mainwindow.h>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <qeventloop.h>
#include <QGroupBox>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class ImageLoader : public QObject {
    Q_OBJECT
public:
    explicit ImageLoader(QObject* parent, ChiakiLog* log, QLabel* label);

    void loadImage(const QString& url);

public slots:
    void onRequestFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* networkManager;
    QLabel* label;
    ChiakiLog* log;
};

#endif //IMAGELOADER_H
