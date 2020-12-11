// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <manualhostdialog.h>
#include <host.h>
#include <settings.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>

ManualHostDialog::ManualHostDialog(Settings *settings, int id, QWidget *parent)
	: QDialog(parent)
{
	this->settings = settings;
	host_id = id;

	setWindowTitle(tr("Add Manual Console"));

	ManualHost host;
	if(id >= 0 && settings->GetManualHostExists(id))
		host = settings->GetManualHost(id);

	auto layout = new QVBoxLayout(this);
	setLayout(layout);

	auto form_layout = new QFormLayout();
	layout->addLayout(form_layout);

	host_edit = new QLineEdit(this);
	host_edit->setText(host.GetHost());
	form_layout->addRow(tr("Host:"), host_edit);
	connect(host_edit, &QLineEdit::textChanged, this, &ManualHostDialog::ValidateInput);

	registered_host_combo_box = new QComboBox(this);
	registered_host_combo_box->addItem(tr("Register on first Connection"));
	auto registered_hosts = settings->GetRegisteredHosts();
	for(const auto &registered_host : registered_hosts)
		registered_host_combo_box->addItem(QString("%1 (%2)").arg(registered_host.GetServerMAC().ToString(), registered_host.GetServerNickname()), QVariant::fromValue(registered_host.GetServerMAC()));
	form_layout->addRow(tr("Registered Console:"), registered_host_combo_box);

	button_box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Discard, this);
	layout->addWidget(button_box);
	connect(button_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(button_box, &QDialogButtonBox::clicked, this, &ManualHostDialog::ButtonClicked);

	ValidateInput();
}

void ManualHostDialog::ValidateInput()
{
	button_box->button(QDialogButtonBox::Save)->setEnabled(!host_edit->text().trimmed().isEmpty());
}

void ManualHostDialog::ButtonClicked(QAbstractButton *button)
{
	if(button_box->buttonRole(button) == QDialogButtonBox::DestructiveRole)
		reject();
}

void ManualHostDialog::accept()
{
	bool registered = false;
	HostMAC registered_mac;
	QVariant registered_host_data = registered_host_combo_box->currentData();
	if(registered_host_data.isValid())
	{
		registered = true;
		registered_mac = registered_host_data.value<HostMAC>();
	}

	ManualHost host(host_id, host_edit->text().trimmed(), registered, registered_mac);
	settings->SetManualHost(host);
	QDialog::accept();
}
