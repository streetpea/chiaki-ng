// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <streamwindow.h>
#include <streamsession.h>
#include <avopenglwidget.h>
#if CHIAKI_GUI_ENABLE_PLACEBO
#include <avplacebowidget.h>
#endif
#include <loginpindialog.h>
#include <settings.h>

#include <QLabel>
#include <QMessageBox>
#include <QCoreApplication>
#include <QAction>
#include <QWindow>
#include <QGuiApplication>
#include <QStandardPaths>

static inline QString GetShaderCacheFile()
{
	return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pl_shader.cache";
}

StreamWindow::StreamWindow(const StreamSessionConnectInfo &connect_info, QWidget *parent)
	: QMainWindow(parent),
	connect_info(connect_info)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_AcceptTouchEvents);
	setWindowTitle(qApp->applicationName() + " | Stream");

	session = nullptr;
	av_widget = nullptr;

	try
	{
		if(connect_info.fullscreen || connect_info.zoom || connect_info.stretch)
			showFullScreen();
		Init();
	}
	catch(const Exception &e)
	{
		QMessageBox::critical(this, tr("Stream failed"), tr("Failed to initialize Stream Session: %1").arg(e.what()));
		close();
	}
}

StreamWindow::~StreamWindow()
{
	// make sure av_widget is always deleted before the session
	delete av_widget;
#if CHIAKI_GUI_ENABLE_PLACEBO
	FILE *file = fopen(qPrintable(GetShaderCacheFile()), "wb");
	if (file) {
		pl_cache_save_file(placebo_cache, file);
		fclose(file);
	}
	pl_cache_destroy(&placebo_cache);
	pl_vulkan_destroy(&placebo_vulkan);
	pl_vk_inst_destroy(&placebo_vk_inst);
	pl_log_destroy(&placebo_log);
#endif
}

void StreamWindow::Init()
{
	session = new StreamSession(connect_info, this);

	connect(session, &StreamSession::SessionQuit, this, &StreamWindow::SessionQuit);
	connect(session, &StreamSession::LoginPINRequested, this, &StreamWindow::LoginPINRequested);

	ResolutionMode resolution_mode;
	if(connect_info.zoom)
		resolution_mode = ResolutionMode::Zoom;
	else if(connect_info.stretch)
		resolution_mode = ResolutionMode::Stretch;
	else
		resolution_mode = ResolutionMode::Normal;

	if(session->GetFfmpegDecoder())
	{
		if (connect_info.settings->GetRenderer() == Renderer::OpenGL)
		{
			auto widget = new AVOpenGLWidget(session, this, resolution_mode);
			widget->HideMouse();
			av_widget=widget;
			setCentralWidget((AVOpenGLWidget*) av_widget);
		}
		else
		{
#if CHIAKI_GUI_ENABLE_PLACEBO
			// Set up Vulkan resources whose lifetime needs to exceed that of the widget
			char** vk_exts = new char*[2]{
				(char*)VK_KHR_SURFACE_EXTENSION_NAME,
				nullptr,
			};

			QString platformName = QGuiApplication::platformName();
			if (platformName == "wayland") {
				vk_exts[1] = (char*)"VK_KHR_wayland_surface";
			} else if (platformName.contains("xcb")) {
				vk_exts[1] = (char*)"VK_KHR_xcb_surface";
			} else {
				// TODO: Bail out?
			}

			const char* opt_extensions[] = {
				VK_EXT_HDR_METADATA_EXTENSION_NAME,
			};

			struct pl_log_params log_params = {
				.log_cb = PlaceboLog,
				.log_priv = session->GetChiakiLog(),
				.log_level = PL_LOG_DEBUG,
			};
			placebo_log = pl_log_create(PL_API_VER, &log_params);

			struct pl_vk_inst_params vk_inst_params = {
				.extensions = vk_exts,
				.num_extensions = 2,
				.opt_extensions = opt_extensions,
				.num_opt_extensions = 1,
			};
			placebo_vk_inst = pl_vk_inst_create(placebo_log, &vk_inst_params);

			auto widget = new AVPlaceboWidget(
				session, placebo_log, placebo_vk_inst,
				resolution_mode, connect_info.settings->GetPlaceboPreset());
			widget->installEventFilter(this);
			VkSurfaceKHR surface = widget->vkSurface();

			struct pl_vulkan_params vulkan_params = {
				.instance = placebo_vk_inst->instance,
				.get_proc_addr = placebo_vk_inst->get_proc_addr,
				.surface = surface,
				.allow_software = true,
				PL_VULKAN_DEFAULTS
			};
			placebo_vulkan = pl_vulkan_create(placebo_log, &vulkan_params);

			struct pl_cache_params cache_params = {
				.log = placebo_log,
				.max_total_size = 10 << 20, // 10 MB
			};
			placebo_cache = pl_cache_create(&cache_params);
			pl_gpu_set_cache(placebo_vulkan->gpu, placebo_cache);
			FILE *file = fopen(qPrintable(GetShaderCacheFile()), "rb");
			if (file) {
				pl_cache_load_file(placebo_cache, file);
				fclose(file);
			}
			widget->setPlaceboVulkan(placebo_vulkan);
			widget->HideMouse();
			auto container_widget = QWidget::createWindowContainer(widget);
			setCentralWidget(container_widget);
			placebo_widget = container_widget;
			av_widget = widget;
#else
			Q_UNREACHABLE();
#endif // CHIAKI_GUI_ENABLE_PLACEBO
		}
	}
	else
	{
		QWidget *bg_widget = new QWidget(this);
		bg_widget->setStyleSheet("background-color: black;");
		setCentralWidget(bg_widget);
	}

	grabKeyboard();

	session->Start();

	auto fullscreen_action = new QAction(tr("Fullscreen"), this);
	fullscreen_action->setShortcut(Qt::Key_F11);
	addAction(fullscreen_action);
	connect(fullscreen_action, &QAction::triggered, this, &StreamWindow::ToggleFullscreen);

	auto stretch_action = new QAction(tr("Stretch"), this);
	stretch_action->setShortcut(Qt::CTRL + Qt::Key_S);
	addAction(stretch_action);
	connect(stretch_action, &QAction::triggered, this, &StreamWindow::ToggleStretch);

	auto zoom_action = new QAction(tr("Zoom"), this);
	zoom_action->setShortcut(Qt::CTRL + Qt::Key_Z);
	addAction(zoom_action);
	connect(zoom_action, &QAction::triggered, this, &StreamWindow::ToggleZoom);

	auto mute_action = new QAction(tr("Toggle Mute"), this);
	mute_action->setShortcut(Qt::CTRL + Qt::Key_M);
	addAction(mute_action);
	connect(mute_action, &QAction::triggered, this, &StreamWindow::ToggleMute);

	auto quit_action = new QAction(tr("Quit"), this);
	quit_action->setShortcut(Qt::CTRL + Qt::Key_Q);
	addAction(quit_action);
	connect(quit_action, &QAction::triggered, this, &StreamWindow::Quit);

	resize(connect_info.video_profile.width, connect_info.video_profile.height);
	show();
}

void StreamWindow::keyPressEvent(QKeyEvent *event)
{
	if(session)
		session->HandleKeyboardEvent(event);
}

void StreamWindow::keyReleaseEvent(QKeyEvent *event)
{
	if(session)
		session->HandleKeyboardEvent(event);
}

bool StreamWindow::event(QEvent *event)
{
	if(session)
	{
		if ((event->type() == QEvent::TouchBegin) || (event->type() == QEvent::TouchUpdate) || (event->type() == QEvent::TouchEnd))
		{
			session->HandleTouchEvent(static_cast<QTouchEvent *>(event));
			return true;
		}
	}
	// hand non-touch events + cancelled touches back to regular handler
	return QMainWindow::event(event);
}

bool StreamWindow::eventFilter(QObject *obj, QEvent *event)
{
	switch(event->type()){
		case QEvent::TouchBegin:
		case QEvent::TouchUpdate:
		case QEvent::TouchEnd:
		case QEvent::MouseButtonDblClick:
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::MouseMove:
		case QEvent::KeyPress:
		case QEvent::KeyRelease:
			QCoreApplication::sendEvent(this, event);
			return true;
		default:
			return false;
	}
}

void StreamWindow::Quit()
{
	close();
}

void StreamWindow::mousePressEvent(QMouseEvent *event)
{
	if(session)
		session->HandleMousePressEvent(event);
}
void StreamWindow::mouseReleaseEvent(QMouseEvent *event)
{
	if(session)
		session->HandleMouseReleaseEvent(event);
}

void StreamWindow::mouseMoveEvent(QMouseEvent *event)
{
	if(session)
		session->HandleMouseMoveEvent(event, frameSize().width(), frameSize().height());
}

void StreamWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
	ToggleFullscreen();

	QMainWindow::mouseDoubleClickEvent(event);
}

void StreamWindow::closeEvent(QCloseEvent *event)
{
	if(session)
	{
		if(session->IsConnected())
		{
			bool sleep = false;
			switch(connect_info.settings->GetDisconnectAction())
			{
				case DisconnectAction::Ask: {
					auto res = QMessageBox::question(this, tr("Disconnect Session"), tr("Do you want the Console to go into sleep mode?"),
							QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
					switch(res)
					{
						case QMessageBox::Yes:
							sleep = true;
							break;
						case QMessageBox::Cancel:
							event->ignore();
							return;
						default:
							break;
					}
					break;
				}
				case DisconnectAction::AlwaysSleep:
					sleep = true;
					break;
				default:
					break;
			}
			if(sleep)
				session->GoToBed();
		}
		session->Stop();
	}
	if (av_widget)
		av_widget->Stop();
}

void StreamWindow::SessionQuit(ChiakiQuitReason reason, const QString &reason_str)
{
	if(chiaki_quit_reason_is_error(reason))
	{
		QString m = tr("Chiaki Session has quit") + ":\n" + chiaki_quit_reason_string(reason);
		if(!reason_str.isEmpty())
			m += "\n" + tr("Reason") + ": \"" + reason_str + "\"";
		QMessageBox::critical(this, tr("Session has quit"), m);
	}
	close();
}

void StreamWindow::LoginPINRequested(bool incorrect)
{

	if(!connect_info.initial_login_pin.isEmpty() && incorrect == false)
	{
		if(!session)
			return;
		session->SetLoginPIN(connect_info.initial_login_pin);
	}
	else
	{
		auto dialog = new LoginPINDialog(incorrect, this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
		connect(dialog, &QDialog::finished, this, [this, dialog](int result) {
			grabKeyboard();

			if(!session)
				return;

			if(result == QDialog::Accepted)
				session->SetLoginPIN(dialog->GetPIN());
			else
				session->Stop();
		});
		releaseKeyboard();
		dialog->show();
	}
}

void StreamWindow::ToggleFullscreen()
{
	if(isFullScreen())
		showNormal();
	else
	{
		showFullScreen();
	}
}

void StreamWindow::ToggleStretch()
{
	if(av_widget)
		av_widget->ToggleStretch();
}

void StreamWindow::ToggleZoom()
{
	if(av_widget)
		av_widget->ToggleZoom();
}

void StreamWindow::ToggleMute()
{
	if(!session)
		return;
	session->ToggleMute();
}

void StreamWindow::resizeEvent(QResizeEvent *event)
{
	UpdateVideoTransform();
	QMainWindow::resizeEvent(event);
}

void StreamWindow::moveEvent(QMoveEvent *event)
{
	UpdateVideoTransform();
	QMainWindow::moveEvent(event);
}

void StreamWindow::changeEvent(QEvent *event)
{
	if(event->type() == QEvent::ActivationChange)
		UpdateVideoTransform();
	QMainWindow::changeEvent(event);
}

void StreamWindow::UpdateVideoTransform()
{
#if CHIAKI_LIB_ENABLE_PI_DECODER
	ChiakiPiDecoder *pi_decoder = session->GetPiDecoder();
	if(pi_decoder)
	{
		QRect r = geometry();
		chiaki_pi_decoder_set_params(pi_decoder, r.x(), r.y(), r.width(), r.height(), isActiveWindow());
	}
#endif
}

#if CHIAKI_GUI_ENABLE_PLACEBO
void StreamWindow::PlaceboLog(void *user, pl_log_level level, const char *msg)
{
    ChiakiLog *log = (ChiakiLog*)user;
    if (!log) {
        return;
    }

    ChiakiLogLevel chiaki_level;
    switch (level)
    {
        case PL_LOG_DEBUG:
            chiaki_level = CHIAKI_LOG_VERBOSE;
            break;
        case PL_LOG_INFO:
            chiaki_level = CHIAKI_LOG_INFO;
            break;
        case PL_LOG_WARN:
            chiaki_level = CHIAKI_LOG_WARNING;
            break;
        case PL_LOG_ERR:
        case PL_LOG_FATAL:
            chiaki_level = CHIAKI_LOG_ERROR;
            break;
    }

    chiaki_log(log, chiaki_level, "[libplacebo] %s", msg);
}
#endif