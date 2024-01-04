#include "sgdbartworkwidget.h"

#include <QFileDialog>
#include <utility>

#include "imageloader.h"

SGDBArtworkWidget::SGDBArtworkWidget(QWidget* parent, ChiakiLog* input_log, ArtworkType input_type, QLabel* display_label, QPushButton* upload_custom_button,
                                     QPushButton* prev_button, QPushButton* next_button, QComboBox* game_combo) {
    index = 0;
    isLoading = true;
    type = input_type;
    log = input_log;

    displayLabel = display_label;
    uploadCustomButton = upload_custom_button;
    gameCombo = game_combo;
    rotateButtons = {{RotateDirection::PREV, prev_button}, {RotateDirection::NEXT, next_button}};
    imageLoader = new ImageLoader(this, log, displayLabel, isLoading);

    //Update the game dropdown for all artworks
    for (auto it = SteamGridDbConstants::gameIDs.constBegin(); it != SteamGridDbConstants::gameIDs.constEnd(); ++it) {
        gameCombo->addItem(it.key(), it.value());
    }

    //Connect the forward and backwards buttons
    for (auto it = rotateButtons.constBegin(); it != rotateButtons.constEnd(); ++it) {
        connect(it.value(), &QPushButton::clicked, this, [it, this]() {
            RotateImage(it.key());
        });
    }

    //Connect the combo_box update
    connect(gameCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        UpdateImageList();
    });

    //Connect the upload button
    connect(uploadCustomButton, &QPushButton::clicked, this, [parent, this]() {
        customImage = QFileDialog::getOpenFileName(parent, tr("Open File"), "", tr("Png Files (*.png)"));
        if (gameCombo->count() == SteamGridDbConstants::gameIDs.size()) {
            //Add the custom item
            gameCombo->addItem("Custom", "custom");
            gameCombo->setCurrentIndex(gameCombo->count()-1);
        }
        artwork = {customImage};
        LoadCustomImage();
    });

    //Load the initial images
    steamGridDb = new SteamGridDb(this);
    connect(steamGridDb, &SteamGridDb::handleArtworkResponse, this, [this](QVector<QString> images) {
        index = 0;
        artwork = std::move(images);
        LoadRemoteImage();
    });
    steamGridDb->getArtwork(log, gameCombo->currentData().toString(), type, 0);
}

void SGDBArtworkWidget::RotateImage(const RotateDirection direction) {
    if (direction == RotateDirection::NEXT) {
        index++;
        //If we've run out of images
        if (index >= artwork.size()) {
            index = 0;
        }
    } else {
        index--;
        //Loop around the bottom
        if (index < 0) {
            index = (artwork.size() - 1);
        }
    }
    LoadRemoteImage();
}

void SGDBArtworkWidget::LoadCustomImage() {
    index = 0;
    const QPixmap pixmap(customImage);
    displayLabel->setPixmap(pixmap);
}

void SGDBArtworkWidget::UpdateImageList() {
    if (gameCombo->currentData().toString() == "custom") {
        artwork = {customImage};
        for (auto it = rotateButtons.constBegin(); it != rotateButtons.constEnd(); ++it) {
            it.value()->setEnabled(false);
        }
        LoadCustomImage();
    } else {
        steamGridDb->getArtwork(log, gameCombo->currentData().toString(), type, 0);
    }
}

void SGDBArtworkWidget::LoadRemoteImage() {
    displayLabel->clear();
    displayLabel->setText("Loading");
    isLoading = true;
    QString url = artwork.at(index);
    imageLoader->loadImage(url);
}

const QPixmap* SGDBArtworkWidget::getPixMap() {
    return displayLabel->pixmap();
}
