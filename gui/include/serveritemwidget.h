// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SERVERITEMWIDGET_H
#define CHIAKI_SERVERITEMWIDGET_H

#include <QFrame>

class QLabel;

class ServerIconWidget;
struct DisplayServer;

class ServerItemWidget : public QFrame
{
	Q_OBJECT

	private:
		bool selected;

		QLabel *top_label;
		QLabel *bottom_label;
		ServerIconWidget *icon_widget;

		QAction *delete_action;
		QAction *wake_action;
		QAction *create_shortcut_action;

	protected:
		void mousePressEvent(QMouseEvent *event) override;
		void mouseDoubleClickEvent(QMouseEvent *event) override;

	public:
		explicit ServerItemWidget(QWidget *parent = nullptr);

		bool IsSelected() { return selected; }
		void SetSelected(bool selected);

		void Update(const DisplayServer &display_server);

	signals:
		void Selected();
		void Triggered();
		void DeleteTriggered();
		void WakeTriggered();
		void CreateShortcutTriggered();
};

#endif //CHIAKI_CONSOLEITEMWIDGET_H
