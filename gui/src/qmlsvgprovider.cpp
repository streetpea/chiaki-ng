#include "qmlsvgprovider.h"

#include <QPainter>
#include <QSvgRenderer>

QmlSvgProvider::QmlSvgProvider()
    : QQuickImageProvider(QQmlImageProviderBase::Image)
{
}

QImage QmlSvgProvider::requestImage(const QString &id, QSize *size, const QSize &requested_size)
{
    const int pos = id.indexOf(QLatin1Char('#'));
    if (pos < 0)
        return {};

    const QString element_name = id.left(pos);
    const QString element_variant = id.mid(pos + 1);

    if (element_name == "console-ps4")
        return drawConsoleImage(":/icons/console-ps4.svg", element_variant, size, requested_size);
    else if (element_name == "console-ps5")
        return drawConsoleImage(":/icons/console-ps5.svg", element_variant, size, requested_size);
    else if (element_name == "button-ps")
        return drawButtonImage(":/icons/buttons-ps.svg", element_variant, size, requested_size);
    else if (element_name == "button-deck")
        return drawButtonImage(":/icons/buttons-deck.svg", element_variant, size, requested_size);
    return {};
}

QImage QmlSvgProvider::drawConsoleImage(const QString &file_name, const QString &state, QSize *size, const QSize &requested_size) const
{
    QSvgRenderer renderer(file_name);
    QRectF view_box = renderer.viewBoxF();
    if (view_box.width() < 0.00001f || view_box.height() < 0.00001f)
        return {};

    QSize img_size = requested_size.isEmpty() ? renderer.defaultSize() : requested_size;
    float icon_aspect = view_box.width() / view_box.height();
    float img_aspect = (float)img_size.width() / (float)img_size.height();
    QRectF icon_rect;
    if (img_aspect > icon_aspect) {
        icon_rect.setHeight(img_size.height());
        icon_rect.setWidth((float)img_size.height() * icon_aspect);
        icon_rect.moveTop(0.0f);
        icon_rect.moveLeft(((float)img_size.width() - icon_rect.width()) * 0.5f);
    } else {
        icon_rect.setWidth(img_size.width());
        icon_rect.setHeight((float)img_size.width() / icon_aspect);
        icon_rect.moveLeft(0.0f);
        icon_rect.moveTop(((float)img_size.height() - icon_rect.height()) * 0.5f);
    }

    QImage img(img_size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);

    auto render_element = [&view_box, &icon_rect, &renderer](QPainter &painter, const QString &id) {
        QRectF src = renderer.boundsOnElement(id);
        QRectF dst = src.translated(-view_box.left(), -view_box.top());
        dst = QRectF(
                icon_rect.width() * dst.left() / view_box.width(),
                icon_rect.height() * dst.top() / view_box.height(),
                icon_rect.width() * dst.width() / view_box.width(),
                icon_rect.height() * dst.height() / view_box.height());
        dst = dst.translated(icon_rect.left(), icon_rect.top());
        renderer.render(&painter, id, dst);
    };
    render_element(painter, state);
    render_element(painter, "console");
    painter.end();

    if (size)
        *size = img_size;

    return img;
}

QImage QmlSvgProvider::drawButtonImage(const QString &file_name, const QString &button, QSize *size, const QSize &requested_size) const
{
    QSvgRenderer renderer(file_name);
    QRectF view_box = renderer.viewBoxF();
    if (view_box.width() < 0.00001f || view_box.height() < 0.00001f)
        return {};

    QSize img_size = requested_size.isEmpty() ? renderer.defaultSize() : requested_size;
    float icon_aspect = view_box.width() / view_box.height();
    float img_aspect = (float)img_size.width() / (float)img_size.height();
    QRectF icon_rect;
    if (img_aspect > icon_aspect) {
        icon_rect.setHeight(img_size.height());
        icon_rect.setWidth((float)img_size.height() * icon_aspect);
        icon_rect.moveTop(0.0f);
        icon_rect.moveLeft(((float)img_size.width() - icon_rect.width()) * 0.5f);
    } else {
        icon_rect.setWidth(img_size.width());
        icon_rect.setHeight((float)img_size.width() / icon_aspect);
        icon_rect.moveLeft(0.0f);
        icon_rect.moveTop(((float)img_size.height() - icon_rect.height()) * 0.5f);
    }

    QImage img(img_size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);

    auto render_element = [&view_box, &icon_rect, &renderer](QPainter &painter, const QString &id) {
        QRectF src = renderer.boundsOnElement(id);
        QRectF dst = src.translated(-view_box.left(), -view_box.top());
        dst = QRectF(
                icon_rect.width() * dst.left() / view_box.width(),
                icon_rect.height() * dst.top() / view_box.height(),
                icon_rect.width() * dst.width() / view_box.width(),
                icon_rect.height() * dst.height() / view_box.height());
        dst = dst.translated(icon_rect.left(), icon_rect.top());
        renderer.render(&painter, id, dst);
    };
    render_element(painter, "circle");
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    render_element(painter, button);
    painter.end();

    if (size)
        *size = img_size;

    return img;
}
