// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_MAINWINDOW_H
#define CHIAKI_MAINWINDOW_H

#include <QMainWindow>

#include "discoverymanager.h"
#include "host.h"

class DynamicGridWidget;
class ServerItemWidget;
class Settings;

struct DisplayServer
{
	DiscoveryHost discovery_host;
	ManualHost manual_host;
	bool discovered;

	RegisteredHost registered_host;
	bool registered;

	QString GetHostAddr() const { return discovered ? discovery_host.host_addr : manual_host.GetHost(); }
	bool IsPS5() const { return discovered ? discovery_host.ps5 :
		(registered ? chiaki_target_is_ps5(registered_host.GetTarget()) : false); }
};

class MainWindow : public QMainWindow
{
	Q_OBJECT

	private:
		Settings *settings;

		QIcon discover_action_icon;
		QIcon discover_action_off_icon;
		QAction *discover_action;

		DynamicGridWidget *grid_widget;
		QList<ServerItemWidget *> server_item_widgets;

		DiscoveryManager discovery_manager;

		QList<DisplayServer> display_servers;

		DisplayServer *DisplayServerFromSender();
		void SendWakeup(const DisplayServer *server);

	private slots:
		void ServerItemWidgetSelected();
		void ServerItemWidgetTriggered();
		void ServerItemWidgetDeleteTriggered();
		void ServerItemWidgetWakeTriggered();

		void UpdateDiscoveryEnabled();
		void ShowSettings();
		void Quit();

		void UpdateDisplayServers();
		void UpdateServerWidgets();

	public:
		explicit MainWindow(Settings *settings, QWidget *parent = nullptr);
		~MainWindow() override;
};

#endif //CHIAKI_MAINWINDOW_H
