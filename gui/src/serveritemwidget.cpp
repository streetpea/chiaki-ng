// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <serveritemwidget.h>
#include <servericonwidget.h>
#include <mainwindow.h>

#include <QLabel>
#include <QVBoxLayout>
#include <QStyle>
#include <QMouseEvent>
#include <QAction>

ServerItemWidget::ServerItemWidget(QWidget *parent) : QFrame(parent)
{
	setFrameStyle(QFrame::Panel);

	auto layout = new QVBoxLayout(this);
	this->setLayout(layout);

	top_label = new QLabel(this);
	top_label->setAlignment(Qt::AlignCenter);
	top_label->setWordWrap(true);
	layout->addWidget(top_label);

	icon_widget = new ServerIconWidget(this);
	layout->addWidget(icon_widget);

	bottom_label = new QLabel(this);
	bottom_label->setAlignment(Qt::AlignCenter);
	bottom_label->setWordWrap(true);
	layout->addWidget(bottom_label);

	setContextMenuPolicy(Qt::ActionsContextMenu);

	delete_action = new QAction(tr("Delete"), this);
	addAction(delete_action);
	connect(delete_action, &QAction::triggered, this, [this]{ emit DeleteTriggered(); });

	wake_action = new QAction(tr("Send Wakeup Packet"), this);
	addAction(wake_action);
	connect(wake_action, &QAction::triggered, this, [this]{ emit WakeTriggered(); });

	this->selected = true;
	SetSelected(false);

	setFixedSize(200, 200);
}

void ServerItemWidget::mousePressEvent(QMouseEvent *event)
{
	emit Selected();
}

void ServerItemWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
	if(event->button() == Qt::LeftButton)
		emit Triggered();
}

void ServerItemWidget::SetSelected(bool selected)
{
	if(this->selected == selected)
		return;
	this->selected = selected;
	setStyleSheet(selected ? "QFrame { background-color: palette(highlight); color: palette(highlighted-text); }" : "");
}

void ServerItemWidget::Update(const DisplayServer &display_server)
{
	delete_action->setEnabled(!display_server.discovered);
	wake_action->setEnabled(display_server.registered);

	icon_widget->SetState(display_server.IsPS5(),
			display_server.discovered ? display_server.discovery_host.state : CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN);

	QString top_text = "";

	if(display_server.discovered || display_server.registered)
	{
		top_text += (display_server.discovered ? display_server.discovery_host.host_name : display_server.registered_host.GetServerNickname()) + "\n";
	}

	top_text += tr("Address: %1").arg(display_server.GetHostAddr());

	if(display_server.discovered || display_server.registered)
	{
		top_text += "\n" + tr("ID: %1 (%2)").arg(
				display_server.discovered ? display_server.discovery_host.GetHostMAC().ToString() : display_server.registered_host.GetServerMAC().ToString(),
				display_server.registered ? tr("registered") : tr("unregistered"));
	}

	top_text += "\n" + (display_server.discovered ? tr("discovered") : tr("manual")),

	top_label->setText(top_text);

	QString bottom_text = "";
	if(display_server.discovered)
	{
		bottom_text += tr("State: %1").arg(chiaki_discovery_host_state_string(display_server.discovery_host.state));
		if(!display_server.discovery_host.running_app_name.isEmpty())
			bottom_text += "\n" + tr("App: %1").arg(display_server.discovery_host.running_app_name);
		if(!display_server.discovery_host.running_app_titleid.isEmpty())
			bottom_text += "\n" + tr("Title ID: %1").arg(display_server.discovery_host.running_app_titleid);
	}
	bottom_label->setText(bottom_text);
}
