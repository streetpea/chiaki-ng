// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <settingsdialog.h>
#include <settings.h>
#include <settingskeycapturedialog.h>
#include <registdialog.h>
#include <sessionlog.h>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QPushButton>
#include <QGroupBox>
#include <QMessageBox>
#include <QComboBox>
#include <QFormLayout>
#include <QMap>
#include <QCheckBox>
#include <QLineEdit>
#include <QtConcurrent>
#include <QFutureWatcher>

#include <chiaki/config.h>
#include <chiaki/ffmpegdecoder.h>

const char * const about_string =
	"<h1>Chiaki</h1> by thestr4ng3r, version " CHIAKI_VERSION
	""
	"<p>This program is free software: you can redistribute it and/or modify "
	"it under the terms of the GNU Affero General Public License version 3 "
	"as published by the Free Software Foundation.</p>"
	""
	"<p>This program is distributed in the hope that it will be useful, "
	"but WITHOUT ANY WARRANTY; without even the implied warranty of "
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
	"GNU General Public License for more details.</p>";

SettingsDialog::SettingsDialog(Settings *settings, QWidget *parent) : QDialog(parent)
{
	this->settings = settings;

	setWindowTitle(tr("Settings"));

	auto root_layout = new QVBoxLayout(this);
	setLayout(root_layout);

	auto horizontal_layout = new QHBoxLayout();
	root_layout->addLayout(horizontal_layout);

	auto left_layout = new QVBoxLayout();
	horizontal_layout->addLayout(left_layout);

	auto right_layout = new QVBoxLayout();
	horizontal_layout->addLayout(right_layout);

	// General

	auto general_group_box = new QGroupBox(tr("General"));
	left_layout->addWidget(general_group_box);

	auto general_layout = new QFormLayout();
	general_group_box->setLayout(general_layout);
	if(general_layout->spacing() < 16)
		general_layout->setSpacing(16);

	log_verbose_check_box = new QCheckBox(this);
	general_layout->addRow(tr("Verbose Logging:\nWarning: This logs A LOT!\nDon't enable for regular use."), log_verbose_check_box);
	log_verbose_check_box->setChecked(settings->GetLogVerbose());
	connect(log_verbose_check_box, &QCheckBox::stateChanged, this, &SettingsDialog::LogVerboseChanged);

	auto log_directory_label = new QLineEdit(GetLogBaseDir(), this);
	log_directory_label->setReadOnly(true);
	general_layout->addRow(tr("Log Directory:"), log_directory_label);

	disconnect_action_combo_box = new QComboBox(this);
	QList<QPair<DisconnectAction, const char *>> disconnect_action_strings = {
		{ DisconnectAction::AlwaysNothing, "Do Nothing"},
		{ DisconnectAction::AlwaysSleep, "Enter Sleep Mode"},
		{ DisconnectAction::Ask, "Ask"}
	};
	auto current_disconnect_action = settings->GetDisconnectAction();
	for(const auto &p : disconnect_action_strings)
	{
		disconnect_action_combo_box->addItem(tr(p.second), (int)p.first);
		if(current_disconnect_action == p.first)
			disconnect_action_combo_box->setCurrentIndex(disconnect_action_combo_box->count() - 1);
	}
	connect(disconnect_action_combo_box, SIGNAL(currentIndexChanged(int)), this, SLOT(DisconnectActionSelected()));

	general_layout->addRow(tr("Action on Disconnect:"), disconnect_action_combo_box);
	
	audio_device_combo_box = new QComboBox(this);
	audio_device_combo_box->addItem(tr("Auto"));
	auto current_audio_device = settings->GetAudioOutDevice();
	if(!current_audio_device.isEmpty())
	{
		// temporarily add the selected device before async fetching is done
		audio_device_combo_box->addItem(current_audio_device, current_audio_device);
		audio_device_combo_box->setCurrentIndex(1);
	}
	connect(audio_device_combo_box, QOverload<int>::of(&QComboBox::activated), this, [this](){
		this->settings->SetAudioOutDevice(audio_device_combo_box->currentData().toString());
	});

	// do this async because it's slow, assuming availableDevices() is thread-safe
	auto audio_devices_future = QtConcurrent::run([]() {
		return QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
	});
	auto audio_devices_future_watcher = new QFutureWatcher<QList<QAudioDeviceInfo>>(this);
	connect(audio_devices_future_watcher, &QFutureWatcher<QList<QAudioDeviceInfo>>::finished, this, [this, audio_devices_future_watcher, settings]() {
		auto available_devices = audio_devices_future_watcher->result();
		while(audio_device_combo_box->count() > 1) // remove all but "Auto"
			audio_device_combo_box->removeItem(1);
		for (QAudioDeviceInfo di : available_devices)
			audio_device_combo_box->addItem(di.deviceName(), di.deviceName());
		int audio_out_device_index = audio_device_combo_box->findData(settings->GetAudioOutDevice());
		audio_device_combo_box->setCurrentIndex(audio_out_device_index < 0 ? 0 : audio_out_device_index);
	});
	audio_devices_future_watcher->setFuture(audio_devices_future);
	general_layout->addRow(tr("Audio Output Device:"), audio_device_combo_box);

	auto about_button = new QPushButton(tr("About Chiaki"), this);
	general_layout->addRow(about_button);
	connect(about_button, &QPushButton::clicked, this, [this]() {
		QMessageBox::about(this, tr("About Chiaki"), about_string);
	});

	// Stream Settings

	auto stream_settings_group_box = new QGroupBox(tr("Stream Settings"));
	left_layout->addWidget(stream_settings_group_box);

	auto stream_settings_layout = new QFormLayout();
	stream_settings_group_box->setLayout(stream_settings_layout);

	resolution_combo_box = new QComboBox(this);
	static const QList<QPair<ChiakiVideoResolutionPreset, const char *>> resolution_strings = {
		{ CHIAKI_VIDEO_RESOLUTION_PRESET_360p, "360p"},
		{ CHIAKI_VIDEO_RESOLUTION_PRESET_540p, "540p"},
		{ CHIAKI_VIDEO_RESOLUTION_PRESET_720p, "720p"},
		{ CHIAKI_VIDEO_RESOLUTION_PRESET_1080p, "1080p (PS5 and PS4 Pro only)"}
	};
	auto current_res = settings->GetResolution();
	for(const auto &p : resolution_strings)
	{
		resolution_combo_box->addItem(tr(p.second), (int)p.first);
		if(current_res == p.first)
			resolution_combo_box->setCurrentIndex(resolution_combo_box->count() - 1);
	}
	connect(resolution_combo_box, SIGNAL(currentIndexChanged(int)), this, SLOT(ResolutionSelected()));
	stream_settings_layout->addRow(tr("Resolution:"), resolution_combo_box);

	fps_combo_box = new QComboBox(this);
	static const QList<QPair<ChiakiVideoFPSPreset, QString>> fps_strings = {
		{ CHIAKI_VIDEO_FPS_PRESET_30, "30" },
		{ CHIAKI_VIDEO_FPS_PRESET_60, "60" }
	};
	auto current_fps = settings->GetFPS();
	for(const auto &p : fps_strings)
	{
		fps_combo_box->addItem(p.second, (int)p.first);
		if(current_fps == p.first)
			fps_combo_box->setCurrentIndex(fps_combo_box->count() - 1);
	}
	connect(fps_combo_box, SIGNAL(currentIndexChanged(int)), this, SLOT(FPSSelected()));
	stream_settings_layout->addRow(tr("FPS:"), fps_combo_box);

	bitrate_edit = new QLineEdit(this);
	bitrate_edit->setValidator(new QIntValidator(2000, 50000, bitrate_edit));
	unsigned int bitrate = settings->GetBitrate();
	bitrate_edit->setText(bitrate ? QString::number(bitrate) : "");
	stream_settings_layout->addRow(tr("Bitrate:"), bitrate_edit);
	connect(bitrate_edit, &QLineEdit::textEdited, this, &SettingsDialog::BitrateEdited);
	UpdateBitratePlaceholder();

	codec_combo_box = new QComboBox(this);
	static const QList<QPair<ChiakiCodec, QString>> codec_strings = {
		{ CHIAKI_CODEC_H264, "H264" },
		{ CHIAKI_CODEC_H265, "H265 (PS5 only)" }
	};
	auto current_codec = settings->GetCodec();
	for(const auto &p : codec_strings)
	{
		codec_combo_box->addItem(p.second, (int)p.first);
		if(current_codec == p.first)
			codec_combo_box->setCurrentIndex(codec_combo_box->count() - 1);
	}
	connect(codec_combo_box, SIGNAL(currentIndexChanged(int)), this, SLOT(CodecSelected()));
	stream_settings_layout->addRow(tr("Codec:"), codec_combo_box);

	audio_buffer_size_edit = new QLineEdit(this);
	audio_buffer_size_edit->setValidator(new QIntValidator(1024, 0x20000, audio_buffer_size_edit));
	unsigned int audio_buffer_size = settings->GetAudioBufferSizeRaw();
	audio_buffer_size_edit->setText(audio_buffer_size ? QString::number(audio_buffer_size) : "");
	stream_settings_layout->addRow(tr("Audio Buffer Size:"), audio_buffer_size_edit);
	audio_buffer_size_edit->setPlaceholderText(tr("Default (%1)").arg(settings->GetAudioBufferSizeDefault()));
	connect(audio_buffer_size_edit, &QLineEdit::textEdited, this, &SettingsDialog::AudioBufferSizeEdited);

	// Decode Settings

	auto decode_settings = new QGroupBox(tr("Decode Settings"));
	left_layout->addWidget(decode_settings);

	auto decode_settings_layout = new QFormLayout();
	decode_settings->setLayout(decode_settings_layout);

#if CHIAKI_LIB_ENABLE_PI_DECODER
	pi_decoder_check_box = new QCheckBox(this);
	pi_decoder_check_box->setChecked(settings->GetDecoder() == Decoder::Pi);
	connect(pi_decoder_check_box, &QCheckBox::toggled, this, [this](bool checked) {
		this->settings->SetDecoder(checked ? Decoder::Pi : Decoder::Ffmpeg);
		UpdateHardwareDecodeEngineComboBox();
	});
	decode_settings_layout->addRow(tr("Use Raspberry Pi Decoder:"), pi_decoder_check_box);
#else
	pi_decoder_check_box = nullptr;
#endif

	hw_decoder_combo_box = new QComboBox(this);
	hw_decoder_combo_box->addItem("none", QString());
	auto current_hw_decoder = settings->GetHardwareDecoder();
	enum AVHWDeviceType hw_dev = AV_HWDEVICE_TYPE_NONE;
	while(true)
	{
		hw_dev = av_hwdevice_iterate_types(hw_dev);
		if(hw_dev == AV_HWDEVICE_TYPE_NONE)
			break;
		QString name = QString::fromUtf8(av_hwdevice_get_type_name(hw_dev));
		hw_decoder_combo_box->addItem(name, name);
		if(current_hw_decoder == name)
			hw_decoder_combo_box->setCurrentIndex(hw_decoder_combo_box->count() - 1);
	}
	connect(hw_decoder_combo_box, SIGNAL(currentIndexChanged(int)), this, SLOT(HardwareDecodeEngineSelected()));
	decode_settings_layout->addRow(tr("Hardware decode method:"), hw_decoder_combo_box);
	UpdateHardwareDecodeEngineComboBox();

	// Registered Consoles

	auto registered_hosts_group_box = new QGroupBox(tr("Registered Consoles"));
	left_layout->addWidget(registered_hosts_group_box);

	auto registered_hosts_layout = new QHBoxLayout();
	registered_hosts_group_box->setLayout(registered_hosts_layout);

	registered_hosts_list_widget = new QListWidget(this);
	registered_hosts_layout->addWidget(registered_hosts_list_widget);

	auto registered_hosts_buttons_layout = new QVBoxLayout();
	registered_hosts_layout->addLayout(registered_hosts_buttons_layout);

	auto register_new_button = new QPushButton(tr("Register New"), this);
	registered_hosts_buttons_layout->addWidget(register_new_button);
	connect(register_new_button, &QPushButton::clicked, this, &SettingsDialog::RegisterNewHost);

	delete_registered_host_button = new QPushButton(tr("Delete"), this);
	registered_hosts_buttons_layout->addWidget(delete_registered_host_button);
	connect(delete_registered_host_button, &QPushButton::clicked, this, &SettingsDialog::DeleteRegisteredHost);

	registered_hosts_buttons_layout->addStretch();

	// Key Settings
	auto key_settings_group_box = new QGroupBox(tr("Key Settings"));
	right_layout->addWidget(key_settings_group_box);
	auto key_horizontal = new QHBoxLayout();
	key_settings_group_box->setLayout(key_horizontal);
	key_horizontal->setSpacing(10);

	auto key_left_form = new QFormLayout();
	auto key_right_form = new QFormLayout();
	key_horizontal->addLayout(key_left_form);
	key_horizontal->addLayout(key_right_form);

	QMap<int, Qt::Key> key_map = this->settings->GetControllerMapping();

	int i = 0;
	for(auto it = key_map.begin(); it != key_map.end(); ++it, ++i)
	{
		int chiaki_button = it.key();
		auto button = new QPushButton(QKeySequence(it.value()).toString(), this);
		button->setAutoDefault(false);
		auto form = i % 2 ? key_left_form : key_right_form;
		form->addRow(Settings::GetChiakiControllerButtonName(chiaki_button), button);
		// Launch key capture dialog on clicked event
		connect(button, &QPushButton::clicked, this, [this, chiaki_button, button](){
			auto dialog = new SettingsKeyCaptureDialog(this);
			// Store captured key to settings and change button label on KeyCaptured event
			connect(dialog, &SettingsKeyCaptureDialog::KeyCaptured, button, [this, button, chiaki_button](Qt::Key key){
						button->setText(QKeySequence(key).toString());
						this->settings->SetControllerButtonMapping(chiaki_button, key);
					});
			dialog->exec();
		});
	}

	// Close Button
	auto button_box = new QDialogButtonBox(QDialogButtonBox::Close, this);
	root_layout->addWidget(button_box);
	connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
	button_box->button(QDialogButtonBox::Close)->setDefault(true);

	UpdateRegisteredHosts();
	UpdateRegisteredHostsButtons();

	connect(settings, &Settings::RegisteredHostsUpdated, this, &SettingsDialog::UpdateRegisteredHosts);
	connect(registered_hosts_list_widget, &QListWidget::itemSelectionChanged, this, &SettingsDialog::UpdateRegisteredHostsButtons);
}

void SettingsDialog::ResolutionSelected()
{
	settings->SetResolution((ChiakiVideoResolutionPreset)resolution_combo_box->currentData().toInt());
	UpdateBitratePlaceholder();
}

void SettingsDialog::DisconnectActionSelected()
{
	settings->SetDisconnectAction(static_cast<DisconnectAction>(disconnect_action_combo_box->currentData().toInt()));
}

void SettingsDialog::LogVerboseChanged()
{
	settings->SetLogVerbose(log_verbose_check_box->isChecked());
}

void SettingsDialog::FPSSelected()
{
	settings->SetFPS((ChiakiVideoFPSPreset)fps_combo_box->currentData().toInt());
}

void SettingsDialog::BitrateEdited()
{
	settings->SetBitrate(bitrate_edit->text().toUInt());
}

void SettingsDialog::CodecSelected()
{
	settings->SetCodec((ChiakiCodec)codec_combo_box->currentData().toInt());
}

void SettingsDialog::AudioBufferSizeEdited()
{
	settings->SetAudioBufferSize(audio_buffer_size_edit->text().toUInt());
}

void SettingsDialog::AudioOutputSelected()
{
	settings->SetAudioOutDevice(audio_device_combo_box->currentText());
}

void SettingsDialog::HardwareDecodeEngineSelected()
{
	settings->SetHardwareDecoder(hw_decoder_combo_box->currentData().toString());
}

void SettingsDialog::UpdateHardwareDecodeEngineComboBox()
{
	hw_decoder_combo_box->setEnabled(settings->GetDecoder() == Decoder::Ffmpeg);
}

void SettingsDialog::UpdateBitratePlaceholder()
{
	bitrate_edit->setPlaceholderText(tr("Automatic (%1)").arg(settings->GetVideoProfile().bitrate));
}

void SettingsDialog::UpdateRegisteredHosts()
{
	registered_hosts_list_widget->clear();
	auto hosts = settings->GetRegisteredHosts();
	for(const auto &host : hosts)
	{
		auto item = new QListWidgetItem(QString("%1 (%2, %3)")
				.arg(host.GetServerMAC().ToString(),
					chiaki_target_is_ps5(host.GetTarget()) ? "PS5" : "PS4",
					host.GetServerNickname()));
		item->setData(Qt::UserRole, QVariant::fromValue(host.GetServerMAC()));
		registered_hosts_list_widget->addItem(item);
	}
}

void SettingsDialog::UpdateRegisteredHostsButtons()
{
	delete_registered_host_button->setEnabled(registered_hosts_list_widget->currentIndex().isValid());
}

void SettingsDialog::RegisterNewHost()
{
	RegistDialog dialog(settings, QString(), this);
	dialog.exec();
}

void SettingsDialog::DeleteRegisteredHost()
{
	auto item = registered_hosts_list_widget->currentItem();
	if(!item)
		return;
	auto mac = item->data(Qt::UserRole).value<HostMAC>();

	int r = QMessageBox::question(this, tr("Delete registered Console"),
			tr("Are you sure you want to delete the registered console with ID %1?").arg(mac.ToString()));
	if(r != QMessageBox::Yes)
		return;

	settings->RemoveRegisteredHost(mac);
}
