#include <QLabel>
#include <imageloader.h>

ImageLoader::ImageLoader(QObject* parent, ChiakiLog* log, QLabel* label, bool& isLoading) : log(log), label(label), isLoading(isLoading), QObject(parent), networkManager(new QNetworkAccessManager(this)) {
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
        isLoading = false;
    } else {
        CHIAKI_LOGE(log, "Failed to load image. Error: %s", reply->errorString().toStdString().c_str());
    }

    // Clean up
    reply->deleteLater();
}
