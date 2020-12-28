// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <servericonwidget.h>

#include <QPainter>
#include <QPainterPath>

ServerIconWidget::ServerIconWidget(QWidget *parent) : QWidget(parent)
{
	LoadSvg();
}

void ServerIconWidget::paintEvent(QPaintEvent *event)
{
	QRectF view_box = svg_renderer.viewBoxF();
	if(view_box.width() < 0.00001f || view_box.height() < 0.00001f)
		return;
	float icon_aspect = view_box.width() / view_box.height();
	float widget_aspect = (float)width() / (float)height();
	QRectF icon_rect;
	if(widget_aspect > icon_aspect)
	{
	        icon_rect.setHeight(height());
	        icon_rect.setWidth((float)height() * icon_aspect);
	        icon_rect.moveTop(0.0f);
	        icon_rect.moveLeft(((float)width() - icon_rect.width()) * 0.5f);
	}
	else
	{
	        icon_rect.setWidth(width());
	        icon_rect.setHeight((float)width() / icon_aspect);
	        icon_rect.moveLeft(0.0f);
	        icon_rect.moveTop(((float)height() - icon_rect.height()) * 0.5f);
	}

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	auto render_element = [&view_box, &icon_rect, this](QPainter &painter, const QString &id) {
		QRectF src = /*svg_renderer.transformForElement(id).mapRect(*/svg_renderer.boundsOnElement(id)/*)*/;
		QRectF dst = src.translated(-view_box.left(), -view_box.top());
		dst = QRectF(
				icon_rect.width() * dst.left() / view_box.width(),
				icon_rect.height() * dst.top() / view_box.height(),
				icon_rect.width() * dst.width() / view_box.width(),
				icon_rect.height() * dst.height() / view_box.height());
		dst = dst.translated(icon_rect.left(), icon_rect.top());
		svg_renderer.render(&painter, id, dst);
	};

	switch(state)
	{
		case CHIAKI_DISCOVERY_HOST_STATE_READY:
			render_element(painter, "light_on");
			break;
		case CHIAKI_DISCOVERY_HOST_STATE_STANDBY:
			render_element(painter, "light_standby");
			break;
		default:
			break;
	}

	auto color = palette().color(QPalette::Text);

	QImage temp_image((int)(devicePixelRatioF() * width()),
			(int)(devicePixelRatioF() * height()),
			QImage::Format_ARGB32_Premultiplied);
	{
		temp_image.fill(QColor(0, 0, 0, 0));
		QPainter temp_painter(&temp_image);
		render_element(temp_painter, "console");
		temp_painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
		temp_painter.fillRect(icon_rect, color);
	}
	painter.drawImage(QRectF(0.0f, 0.0f, width(), height()), temp_image);
}

void ServerIconWidget::LoadSvg()
{
	QString path = ps5 ? ":/icons/console-ps5.svg" : ":/icons/console-ps4.svg";
	svg_renderer.load(path);
	update();
}

void ServerIconWidget::SetState(bool ps5, ChiakiDiscoveryHostState state)
{
	bool reload = this->ps5 != ps5;
	this->ps5 = ps5;
	this->state = state;
	if(reload)
		LoadSvg();
	update();
}
