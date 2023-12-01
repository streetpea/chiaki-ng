// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_GUI_STREAMWINDOW_H
#define CHIAKI_GUI_STREAMWINDOW_H

#include <QMainWindow>
#if CHIAKI_GUI_ENABLE_PLACEBO
#include <libplacebo/log.h>
#include <libplacebo/vulkan.h>
#include <libplacebo/cache.h>
#endif

#include "streamsession.h"
#include "avwidget.h"

class QLabel;

class StreamWindow: public QMainWindow
{
	Q_OBJECT

	public:
		explicit StreamWindow(const StreamSessionConnectInfo &connect_info, QWidget *parent = nullptr);
		~StreamWindow() override;

	private:
		const StreamSessionConnectInfo connect_info;
		StreamSession *session;
		IAVWidget *av_widget;

#if CHIAKI_GUI_ENABLE_PLACEBO
		// Needs to be placed here since the Vulkan instance needs
		// to be kept alive until the QWindow destructor has been run, i.e.
		// we can't tie it to the lifetime of the AVWidget since we'd tear
		// it down too early
		pl_log placebo_log;
		pl_vk_inst placebo_vk_inst;
		pl_vulkan placebo_vulkan;
		pl_cache placebo_cache;
		QWidget *placebo_widget;
#endif

		void Init();
		void UpdateVideoTransform();

#if CHIAKI_GUI_ENABLE_PLACEBO
		static void PlaceboLog(void *user, pl_log_level level, const char *msg);
#endif

	protected:
		void keyPressEvent(QKeyEvent *event) override;
		void keyReleaseEvent(QKeyEvent *event) override;
		bool event(QEvent *event) override;
		bool eventFilter(QObject *obj, QEvent *event) override;
		void closeEvent(QCloseEvent *event) override;
		void mousePressEvent(QMouseEvent *event) override;
		void mouseReleaseEvent(QMouseEvent *event) override;
		void mouseMoveEvent(QMouseEvent *event) override;
		void mouseDoubleClickEvent(QMouseEvent *event) override;
		void resizeEvent(QResizeEvent *event) override;
		void moveEvent(QMoveEvent *event) override;
		void changeEvent(QEvent *event) override;

	private slots:
		void SessionQuit(ChiakiQuitReason reason, const QString &reason_str);
		void LoginPINRequested(bool incorrect);
		void ToggleFullscreen();
		void ToggleStretch();
		void ToggleZoom();
		void ToggleMute();
		void Quit();
};

#endif // CHIAKI_GUI_STREAMWINDOW_H
