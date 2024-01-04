#ifndef SGDBARTWORKWIDGET_H
#define SGDBARTWORKWIDGET_H
#include <QComboBox>
#include <QLabel>
#include <QMap>
#include <QObject>
#include <QPushButton>

#include "imageloader.h"
#include "sgdbenums.h"
#include "steamgriddbapi.h"
#include "chiaki/log.h"

class SGDBArtworkWidget : public QObject {
    Q_OBJECT
    public:
        explicit SGDBArtworkWidget(QWidget* parent, ChiakiLog* log, ArtworkType type, QLabel* displayLabel, QPushButton* uploadCustomButton, QPushButton* prevButton, QPushButton* nextButton, QComboBox* gameCombo);

        //Descriptors
        ChiakiLog* log;
        ArtworkType type;
        int index;
        QVector<QString> artwork;
        QString customImage;
        bool isLoading;

        //Widgets
        QLabel *displayLabel;
        QPushButton *uploadCustomButton;
        QMap<RotateDirection, QPushButton*> rotateButtons;
        QComboBox *gameCombo;
        ImageLoader *imageLoader;
        SteamGridDb *steamGridDb;

    const QPixmap* getPixMap();

    public slots:
        void RotateImage(RotateDirection direction);
        void UpdateImageList();
        void LoadCustomImage();
        void LoadRemoteImage();
};



#endif //SGDBARTWORKWIDGET_H
