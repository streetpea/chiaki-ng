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
    ImageLoader(QLabel* label) : label(label) {}

    void loadImage(const QString& url) {
        QNetworkAccessManager* manager = new QNetworkAccessManager(this);
        QUrl qurl = QUrl(url);
        QNetworkRequest request(qurl);

        QNetworkReply* reply = manager->get(request);

        // Use a QEventLoop to wait for the image to be downloaded
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() == QNetworkReply::NoError) {
            // Load the image into QPixmap and set it as the label's pixmap
            QPixmap pixmap;
            pixmap.loadFromData(reply->readAll());
            label->setPixmap(pixmap);
        } else {
            // Handle error if needed
            qWarning() << "Failed to load image. Error:" << reply->errorString();
        }

        // Clean up
        reply->deleteLater();
    }

private:
    QLabel* label;
};

#endif //IMAGELOADER_H
