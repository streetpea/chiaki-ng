// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SERVERICONWIDGET_H
#define CHIAKI_SERVERICONWIDGET_H

#include <chiaki/discovery.h>

#include <QWidget>
#include <QSvgRenderer>

class ServerIconWidget : public QWidget
{
	Q_OBJECT

	private:
		bool ps5 = false;
		ChiakiDiscoveryHostState state = CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
		QSvgRenderer svg_renderer;

		void LoadSvg();

	protected:
		void paintEvent(QPaintEvent *event) override;

	public:
		explicit ServerIconWidget(QWidget *parent = nullptr);

		void SetState(bool ps5, ChiakiDiscoveryHostState state);
};

#endif // CHIAKI_SERVERICONWIDGET_H
