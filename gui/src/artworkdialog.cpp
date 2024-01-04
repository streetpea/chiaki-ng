#include "artworkdialog.h"

#include <QMessageBox>
#include <qmetaobject.h>

ArtworkDialog::ArtworkDialog(QWidget* parent) {
    setupUi(this);

    //Widgets
    artworkWidgets.insert(ArtworkType::LANDSCAPE,
        new SGDBArtworkWidget(this, &log, ArtworkType::LANDSCAPE, landscape_label, landscape_upload_button, landscape_prev_button, landscape_next_button, landscape_game_combo));
    artworkWidgets.insert(ArtworkType::PORTRAIT,
        new SGDBArtworkWidget(this, &log, ArtworkType::PORTRAIT, portrait_label, portrait_upload_button, portrait_prev_button, portrait_next_button, portrait_game_combo));
    artworkWidgets.insert(ArtworkType::HERO,
        new SGDBArtworkWidget(this, &log, ArtworkType::HERO, hero_label, hero_upload_button, hero_prev_button, hero_next_button, hero_game_combo));
    artworkWidgets.insert(ArtworkType::ICON,
        new SGDBArtworkWidget(this, &log, ArtworkType::ICON, icon_label, icon_upload_button, icon_prev_button, icon_next_button, icon_game_combo));
    artworkWidgets.insert(ArtworkType::LOGO,
        new SGDBArtworkWidget(this, &log, ArtworkType::LOGO, logo_label, logo_upload_button, logo_prev_button, logo_next_button, logo_game_combo));

    connect(cancel_button, &QPushButton::clicked, [this](){QDialog::reject();});
    connect(ok_button, &QPushButton::clicked, this, &ArtworkDialog::success);
}

QString stringFromType(ArtworkType type) {
    switch (type) {
        case ArtworkType::LANDSCAPE:
            return "landscape";
        case ArtworkType::PORTRAIT:
            return "portrait";
        case ArtworkType::HERO:
            return "hero";
        case ArtworkType::ICON:
            return "icon";
        case ArtworkType::LOGO:
            return "logo";
        default:
            return "";
    }
}

void ArtworkDialog::success() {
    bool isLoading = false;
    QMap<QString, const QPixmap*> returnMap;

    for (auto it = artworkWidgets.begin(); it != artworkWidgets.end(); ++it) {
        returnMap.insert(stringFromType(it.key()), it.value()->getPixMap());
        isLoading = isLoading || it.value()->isLoading;
        if (isLoading) {
            QMessageBox::critical(this, "Artwork Loading", "Please wait until all artwork has loaded");
            return;
        }
    }

    emit updateArtwork(returnMap);

    QDialog::accept();
}
