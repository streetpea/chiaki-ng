#include "psnlogindialog.h"

#include <QVBoxLayout>
#include <QWebEngineSettings>

PSNLoginDialog::PSNLoginDialog(Settings *settings, RegistDialog *parent) : QDialog(parent) {
    chiaki_log_init(&log, settings->GetLogLevelMask(), chiaki_log_cb_print, this);
    setWindowTitle(tr("Playstation Login"));
    resize(800, 600); // Adjust the size as needed
    web_engine_view = new QWebEngineView(this);

    // Get the QWebEngineSettings of the view
    QWebEngineSettings *web_engine_settings = web_engine_view->settings();

    // Disable WebGL
    web_engine_settings->setAttribute(QWebEngineSettings::WebGLEnabled, false);

    web_engine_view->setUrl(QUrl(PSNAuth::LOGIN_URL));

    connect(web_engine_view, &QWebEngineView::loadFinished, this, &PSNLoginDialog::handleWebEngineLoadFinished);

    QVBoxLayout* layout = new QVBoxLayout();
    layout->addWidget(web_engine_view);

    setLayout(layout);
}

void PSNLoginDialog::handleWebEngineLoadFinished(bool) {
    QString redirectCode;


    QString windowTitle;
    if (web_engine_view->url().toString().toStdString().rfind("https", 0) == 0) {
        //We know the page is secure here because the QWebEngineView won't accept ivalid certificates.
        windowTitle.append("🔒 ");
    }
    windowTitle.append(web_engine_view->url().toString());
    setWindowTitle(windowTitle);

    if (web_engine_view->url().toString().toStdString().compare(0, PSNAuth::REDIRECT_PAGE.length(), PSNAuth::REDIRECT_PAGE) == 0) {
        QString queryParam = web_engine_view->url().query();

        size_t codePos = queryParam.indexOf("code=");

        // Extract the substring starting from the position after 'code='
        redirectCode = queryParam.mid(codePos + 5); // 5 is the length of "code="

        // Find the position of '&' to exclude other parameters
        size_t ampersandPos = redirectCode.indexOf('&');
        if (ampersandPos != std::string::npos) {
            redirectCode = redirectCode.mid(0, ampersandPos);
        }

        web_engine_view->close();

        PSNAccountID* psn_account_id = new PSNAccountID(this);
        connect(psn_account_id, &PSNAccountID::AccountIDResponse, this, &PSNLoginDialog::handlePsnAccountIdResponse);

        psn_account_id->GetPsnAccountId(redirectCode);
    }
}

void PSNLoginDialog::handlePsnAccountIdResponse(QString accountId) {
    RegistDialog* parentDialog = qobject_cast<RegistDialog*>(parent());
    if (parentDialog) {
        parentDialog->updatePsnAccountID(accountId);
        close();
    }
}
