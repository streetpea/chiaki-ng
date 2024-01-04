#ifndef ARTWORKDIALOG_H
#define ARTWORKDIALOG_H

#include <ui_artworkdialog.h>

#include "sgdbartworkwidget.h"
#include "sgdbenums.h"
#include "chiaki/log.h"


class ArtworkDialog : public QDialog, private Ui::ArtworkDialog {
    Q_OBJECT

    private:
        ChiakiLog log;
        QMap<ArtworkType, SGDBArtworkWidget*> artworkWidgets;


    public:
        ArtworkDialog(QWidget* parent = nullptr);

    public slots:
        void success();

    signals:
        void updateArtwork(QMap<QString, const QPixmap*> artwork);
};



#endif //ARTWORKDIALOG_H
