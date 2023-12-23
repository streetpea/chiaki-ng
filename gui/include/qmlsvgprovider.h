#pragma once

#include <QQuickImageProvider>

class QmlSvgProvider : public QQuickImageProvider
{
public:
    explicit QmlSvgProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requested_size) override;

private:
    QImage drawConsoleImage(const QString &file_name, const QString &state, QSize *size, const QSize &requested_size) const;
};
