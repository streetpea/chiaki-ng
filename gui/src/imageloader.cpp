#include <QLabel>
#include <imageloader.h>

ImageLoader::ImageLoader(QObject* parent, ChiakiLog* input_log, QLabel* input_label) : QObject(parent), networkManager(new QNetworkAccessManager(this)) {
    label = input_label;
    log = input_log;

    connect(networkManager, &QNetworkAccessManager::finished, this, &ImageLoader::onRequestFinished);
}

void ImageLoader::loadImage(const QString& url) {
    {
        QUrl qurl = QUrl(url);
        QNetworkRequest request(qurl);

        networkManager->get(request);
    }
}

void ImageLoader::onRequestFinished(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        // Load the image into QPixmap and set it as the label's pixmap
        QPixmap pixmap;
        pixmap.loadFromData(reply->readAll());
        label->setPixmap(pixmap);
        label->setText("");
    } else {
        // Handle error if needed
        CHIAKI_LOGE(log, "Failed to load image. Error: %s", reply->errorString());
    }

    // Clean up
    reply->deleteLater();
}
