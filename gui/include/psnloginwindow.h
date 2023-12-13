//
// Created by Jamie Bartlett on 13/12/2023.
//

#ifndef PSNLIGINWINDOW_H
#define PSNLIGINWINDOW_H

#include <QLabel>
#include <QMainWindow>
#include <QSurfaceFormat>
#include <QWebEngineView>

#include <qeventloop.h>
#include "registdialog.h"
#include "psnaccountid.h"

class PSNLoginWindow : public QMainWindow {
    Q_OBJECT

private:
    QWebEngineView* web_engine_view;

public:
    PSNLoginWindow(RegistDialog *parent = nullptr);

    private slots:
        void handleWebEngineLoadFinished(bool);

};



#endif //PSNLIGINWINDOW_H
