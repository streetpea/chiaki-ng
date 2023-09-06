// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <streamwindow.h>
#include <streamsession.h>
#include <avopenglwidget.h>
#include <loginpindialog.h>
#include <settings.h>

#include <QLabel>
#include <QMessageBox>
#include <QCoreApplication>
#include <QAction>

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
}

void StreamWindow::Init()
{
	session = new StreamSession(connect_info, this);

	connect(session, &StreamSession::SessionQuit, this, &StreamWindow::SessionQuit);
	connect(session, &StreamSession::LoginPINRequested, this, &StreamWindow::LoginPINRequested);

	AVOpenGLWidget::ResolutionMode resolution_mode;
	if(connect_info.zoom)
		resolution_mode = AVOpenGLWidget::Zoom;
	else if(connect_info.stretch)
		resolution_mode = AVOpenGLWidget::Stretch;
	else
		resolution_mode = AVOpenGLWidget::Normal;

	if(session->GetFfmpegDecoder())
	{
		av_widget = new AVOpenGLWidget(session, this, resolution_mode);
		setCentralWidget(av_widget);
		av_widget->HideMouse();
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
