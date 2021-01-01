// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "settingskeycapturedialog.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QKeyEvent>

SettingsKeyCaptureDialog::SettingsKeyCaptureDialog(QWidget *parent)
{
	setWindowTitle(tr("Key Capture"));

	auto root_layout = new QVBoxLayout(this);
	setLayout(root_layout);

	auto label = new QLabel(tr("Press any key to configure button or click close."));
	root_layout->addWidget(label);

	auto button = new QPushButton(tr("Close"), this);
	root_layout->addWidget(button);
	button->setAutoDefault(false);
	connect(button, &QPushButton::clicked, this, &QDialog::accept);
}

void SettingsKeyCaptureDialog::keyReleaseEvent(QKeyEvent *event)
{
	KeyCaptured(Qt::Key(event->key()));
	accept();
}
