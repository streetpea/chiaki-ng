// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_REGISTDIALOG_H
#define CHIAKI_REGISTDIALOG_H

#include <chiaki/regist.h>

#include "host.h"

#include <QDialog>

class Settings;

class QLineEdit;
class QPlainTextEdit;
class QDialogButtonBox;
class QCheckBox;
class QRadioButton;

class RegistDialog : public QDialog
{
	Q_OBJECT

	private:
		Settings *settings;

		QLineEdit *host_edit;
		QCheckBox *broadcast_check_box;
		QRadioButton *ps4_pre9_radio_button;
		QRadioButton *ps4_pre10_radio_button;
		QRadioButton *ps4_10_radio_button;
		QRadioButton *ps5_radio_button;
		QLineEdit *psn_online_id_edit;
		QLineEdit *psn_account_id_edit;
		QLineEdit *pin_edit;
		QDialogButtonBox *button_box;
		QPushButton *register_button;

		RegisteredHost registered_host;

		bool NeedAccountId();

	private slots:
		void ValidateInput();

	public:
		explicit RegistDialog(Settings *settings, const QString &host = QString(), QWidget *parent = nullptr);
		~RegistDialog();

		RegisteredHost GetRegisteredHost() { return registered_host; }

	public slots:
		void accept() override;
};

class RegistExecuteDialog: public QDialog
{
	Q_OBJECT

	friend class RegistExecuteDialogPrivate;

	private:
		Settings *settings;

		ChiakiLog log;
		ChiakiRegist regist;

		QPlainTextEdit *log_edit;
		QDialogButtonBox *button_box;

		RegisteredHost registered_host;

		void Finished();

	private slots:
		void Log(ChiakiLogLevel level, QString msg);
		void Success(RegisteredHost host);
		void Failed();

	public:
		explicit RegistExecuteDialog(Settings *settings, const ChiakiRegistInfo &regist_info, QWidget *parent = nullptr);
		~RegistExecuteDialog();

		RegisteredHost GetRegisteredHost()	{ return registered_host; }
};

Q_DECLARE_METATYPE(ChiakiRegistEventType)

#endif // CHIAKI_REGISTDIALOG_H
