#include "psnloginwindow.h"

PSNLoginWindow::PSNLoginWindow(Settings *settings, RegistDialog *parent) : QMainWindow(parent) {
    chiaki_log_init(&log, settings->GetLogLevelMask(), chiaki_log_cb_print, this);
    setWindowTitle(tr("Playstation Login"));
    setFixedSize(800, 600); // Adjust the size as needed
    web_engine_view = new QWebEngineView(this);

    web_engine_view->setUrl(QUrl(QString::fromStdString(PSNAuth::LOGIN_URL)));

    connect(web_engine_view, &QWebEngineView::loadFinished, this, &PSNLoginWindow::handleWebEngineLoadFinished);

    setCentralWidget(web_engine_view);
}

void PSNLoginWindow::handleWebEngineLoadFinished(bool) {
    std::string redirectCode;

    if (web_engine_view->url().toString().toStdString().compare(0, PSNAuth::REDIRECT_PAGE.length(), PSNAuth::REDIRECT_PAGE) == 0) {
        std::string queryParam = web_engine_view->url().query().toStdString();

        size_t codePos = queryParam.find("code=");

        // Extract the substring starting from the position after 'code='
        redirectCode = queryParam.substr(codePos + 5); // 5 is the length of "code="

        // Find the position of '&' to exclude other parameters
        size_t ampersandPos = redirectCode.find('&');
        if (ampersandPos != std::string::npos) {
            redirectCode = redirectCode.substr(0, ampersandPos);
        }

        web_engine_view->close();

        std::string accessToken = PSNAccountID::getAccessToken(&log, redirectCode);
        std::string userId = PSNAccountID::GetAccountInfo(&log, accessToken);

        RegistDialog* parentDialog = qobject_cast<RegistDialog*>(parent());
        if (parentDialog) {
            parentDialog->updatePsnAccountID(userId);
            close();
        }
    }
}