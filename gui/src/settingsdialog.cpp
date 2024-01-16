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
#include <QScrollArea>
#include <QMap>
#include <QScrollArea>
#include <QCheckBox>
#include <QLineEdit>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QTabWidget>
#if CHIAKI_GUI_ENABLE_SPEEX
#include <QSlider>
#include <QLabel>
#endif
#include <SDL.h>

#include <chiaki/config.h>
#include <chiaki/ffmpegdecoder.h>

const char * const about_string =
	"<h1>chiaki4deck</h1> by Street Pea, version " CHIAKI_VERSION
	""
	"<h2>Fork of Chiaki</h2> by Florian Markl at version 2.1.1"
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
	root_layout->setContentsMargins(4, 4, 4, 4);
	root_layout->setSpacing(4);
	setLayout(root_layout);

	auto scroll_area = new QScrollArea(this);
	scroll_area->setWidgetResizable(true);
	scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	root_layout->addWidget(scroll_area);

	auto tab_widget = new QTabWidget(this);
	tab_widget->setTabPosition(QTabWidget::West);
	tab_widget->tabBar()->setIconSize(QSize(32, 32));
	scroll_area->setWidget(tab_widget);

	auto scroll_content_widget = new QWidget(scroll_area);
	resize(1280, 800);
	auto horizontal_layout = new QHBoxLayout();
	scroll_content_widget->setLayout(horizontal_layout);
	tab_widget->addTab(scroll_content_widget, QIcon(":icons/settings-20px.svg"), tr("General"));

	auto left_layout = new QVBoxLayout();
	horizontal_layout->addLayout(left_layout);

	auto right_layout = new QVBoxLayout();
	horizontal_layout->addLayout(right_layout);

	QWidget *other_tab = new QWidget();
	tab_widget->addTab(other_tab, QIcon(":icons/discover-24px.svg"), tr("Stream && Consoles"));

	auto other_layout = new QVBoxLayout();
	other_tab->setLayout(other_layout);

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

	dualsense_check_box = new QCheckBox(this);
	general_layout->addRow(tr("PS5 Features [Experimental]:\nEnable haptics and adaptive triggers\nfor attached DualSense controllers\nand haptics for Steam Deck\nif no DualSense attached."), dualsense_check_box);
	dualsense_check_box->setChecked(settings->GetDualSenseEnabled());
	connect(dualsense_check_box, &QCheckBox::stateChanged, this, &SettingsDialog::DualSenseChanged);

	buttons_pos_check_box = new QCheckBox(this);
	general_layout->addRow(tr("Use buttons by position\n instead of by label."), buttons_pos_check_box);
	buttons_pos_check_box->setChecked(settings->GetButtonsByPosition());
	connect(buttons_pos_check_box, &QCheckBox::stateChanged, this, &SettingsDialog::ButtonsPosChanged);

	vertical_sdeck_check_box = new QCheckBox(this);
	general_layout->addRow(tr("Use Steam Deck in vertical\norientation (for motion controls)."), vertical_sdeck_check_box);
	vertical_sdeck_check_box->setChecked(settings->GetVerticalDeckEnabled());
	connect(vertical_sdeck_check_box, &QCheckBox::stateChanged, this, &SettingsDialog::DeckOrientationChanged);

	automatic_connect_check_box = new QCheckBox(this);
	general_layout->addRow(tr("Automatically connect to PlayStation after clicking in GUI."), automatic_connect_check_box);
	automatic_connect_check_box->setChecked(settings->GetAutomaticConnect());
	connect(automatic_connect_check_box, &QCheckBox::stateChanged, this, &SettingsDialog::AutomaticConnectChanged);

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
	audio_device_combo_box->setSizeAdjustPolicy(QComboBox::AdjustToContents);
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
		QStringList out;
		const int count = SDL_GetNumAudioDevices(false);
		for(int i = 0; i < count; i++)
			out.append(SDL_GetAudioDeviceName(i, false));
		return out;
	});
	auto audio_devices_future_watcher = new QFutureWatcher<QStringList>(this);
	connect(audio_devices_future_watcher, &QFutureWatcher<QStringList>::finished, this, [this, audio_devices_future_watcher, settings]() {
		auto available_devices = audio_devices_future_watcher->result();
		while(audio_device_combo_box->count() > 1) // remove all but "Auto"
			audio_device_combo_box->removeItem(1);
		for(auto di : available_devices)
			audio_device_combo_box->addItem(di, di);
		int audio_out_device_index = audio_device_combo_box->findData(settings->GetAudioOutDevice());
		audio_device_combo_box->setCurrentIndex(audio_out_device_index < 0 ? 0 : audio_out_device_index);
	});
	audio_devices_future_watcher->setFuture(audio_devices_future);
	general_layout->addRow(tr("Audio Output Device:"), audio_device_combo_box);

	microphone_combo_box = new QComboBox(this);
	microphone_combo_box->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	microphone_combo_box->addItem(tr("Auto"));
	auto current_microphone = settings->GetAudioInDevice();
	if(!current_microphone.isEmpty())
	{
		// temporarily add the selected device before async fetching is done
		microphone_combo_box->addItem(current_microphone, current_microphone);
		microphone_combo_box->setCurrentIndex(1);
	}
	connect(microphone_combo_box, QOverload<int>::of(&QComboBox::activated), this, [this](){
		this->settings->SetAudioInDevice(microphone_combo_box->currentData().toString());
	});

	// do this async because it's slow, assuming availableDevices() is thread-safe
	auto microphones_future = QtConcurrent::run([]() {
		QStringList out;
		const int count = SDL_GetNumAudioDevices(true);
		for(int i = 0; i < count; i++)
			out.append(SDL_GetAudioDeviceName(i, true));
		return out;
	});
	auto microphones_future_watcher = new QFutureWatcher<QStringList>(this);
	connect(microphones_future_watcher, &QFutureWatcher<QStringList>::finished, this, [this, microphones_future_watcher, settings]() {
		auto available_microphones = microphones_future_watcher->result();
		while(microphone_combo_box->count() > 1) // remove all but "Auto"
			microphone_combo_box->removeItem(1);
		for(auto di : available_microphones)
			microphone_combo_box->addItem(di, di);
		int audio_in_device_index = microphone_combo_box->findData(settings->GetAudioInDevice());
		microphone_combo_box->setCurrentIndex(audio_in_device_index < 0 ? 0 : audio_in_device_index);
	});
	microphones_future_watcher->setFuture(microphones_future);
	general_layout->addRow(tr("Microphone:"), microphone_combo_box);

#if CHIAKI_GUI_ENABLE_SPEEX
	speech_processing_check_box = new QCheckBox(this);
	general_layout->addRow(tr("Enable speech processing:\nnoise suppression + echo cancellation."), speech_processing_check_box);
	speech_processing_check_box->setChecked(settings->GetSpeechProcessingEnabled());
	connect(speech_processing_check_box, &QCheckBox::stateChanged, this, &SettingsDialog::SpeechProcessingChanged);

	noiseSuppress = new QSlider(this);
	noiseSuppress->setRange(0, 60);
	noiseSuppress->setValue(settings->GetNoiseSuppressLevel());
	noiseValue = new QLabel(QString::number(settings->GetNoiseSuppressLevel()));
	noiseSuppress->setOrientation(Qt::Horizontal);
	connect(noiseSuppress, &QSlider::valueChanged, this, [this](){
		this->settings->SetNoiseSuppressLevel(noiseSuppress->value());
	});
	connect(noiseSuppress, &QSlider::valueChanged, [this](int value) {
		noiseValue->setText(QString::number(value));
	});
	general_layout->addRow(tr("Noise to Suppress (dB)"), noiseSuppress);
	general_layout->addWidget(noiseValue);

	echoSuppress = new QSlider(this);
	echoSuppress->setRange(0, 60);
	echoSuppress->setValue(settings->GetEchoSuppressLevel());
	echoValue = new QLabel(QString::number(settings->GetEchoSuppressLevel()));
	echoSuppress->setOrientation(Qt::Horizontal);
	connect(echoSuppress, &QSlider::valueChanged, this, [this](){
		this->settings->SetEchoSuppressLevel(echoSuppress->value());
	});
	connect(echoSuppress, &QSlider::valueChanged, [this](int value) {
		echoValue->setText(QString::number(value));
	});
	general_layout->addRow(tr("Echo to Suppress (dB)"), echoSuppress);
	general_layout->addWidget(echoValue);
#endif

	auto about_button = new QPushButton(tr("About chiaki4deck"), this);
	general_layout->addRow(about_button);
	connect(about_button, &QPushButton::clicked, this, [this]() {
		QMessageBox::about(this, tr("About chiaki4deck"), about_string);
	});

	// Stream Settings

	auto stream_settings_group_box = new QGroupBox(tr("Stream Settings"));
	other_layout->addWidget(stream_settings_group_box);

	auto stream_settings_layout = new QFormLayout();
	stream_settings_group_box->setLayout(stream_settings_layout);

	resolution_combo_box = new QComboBox(this);
	static const QList<QPair<ChiakiVideoResolutionPreset, const char *>> resolution_strings = {
		{ CHIAKI_VIDEO_RESOLUTION_PRESET_360p, "360p" },
		{ CHIAKI_VIDEO_RESOLUTION_PRESET_540p, "540p" },
		{ CHIAKI_VIDEO_RESOLUTION_PRESET_720p, "720p" },
		{ CHIAKI_VIDEO_RESOLUTION_PRESET_1080p, "1080p (PS5 and PS4 Pro only)" }
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
		{ CHIAKI_CODEC_H265, "H265 (PS5 only)" },
#if CHIAKI_GUI_ENABLE_PLACEBO
		{ CHIAKI_CODEC_H265_HDR, "H265 HDR (PS5 only)" }
#endif
	};
	auto current_codec = settings->GetCodec();
	for(const auto &p : codec_strings)
	{
		// HDR is only supported with Placebo Vulkan renderer
		if (p.first == CHIAKI_CODEC_H265_HDR && settings->GetRenderer() != Renderer::PlaceboVk)
			continue;

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
	other_layout->addWidget(decode_settings);

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

#if CHIAKI_GUI_ENABLE_PLACEBO
	// Renderer Settings
	auto renderer_settings = new QGroupBox(tr("Renderer Settings"));
	other_layout->addWidget(renderer_settings);

	renderer_settings_layout = new QFormLayout();
	renderer_settings->setLayout(renderer_settings_layout);

	renderer_combo_box = new QComboBox(this);
	static const QList<QPair<Renderer, QString>> renderer_strings = {
		{ Renderer::OpenGL, "OpenGL" },
		{ Renderer::PlaceboVk, "Placebo (Vulkan, needed for HDR)" }
	};

	auto current_renderer = settings->GetRenderer();
	for(const auto &p : renderer_strings)
	{
		renderer_combo_box->addItem(p.second, (int)p.first);
		if(current_renderer == p.first)
			renderer_combo_box->setCurrentIndex(renderer_combo_box->count() - 1);
	}
	connect(renderer_combo_box, SIGNAL(currentIndexChanged(int)), this, SLOT(RendererSelected()));
	renderer_settings_layout->addRow(tr("Renderer:"), renderer_combo_box);

	placebo_preset_combo_box = new QComboBox(this);
	static const QList<QPair<PlaceboPreset, QString>> placebo_preset_strings = {
		{ PlaceboPreset::Default, "Default (balance of performance and quality)" },
		{ PlaceboPreset::Fast, "Fast (no advanced rendering features)" },
		{ PlaceboPreset::HighQuality, "High Quality" }
	};
	auto current_placebo_preset = settings->GetPlaceboPreset();
	for(const auto &p : placebo_preset_strings)
	{
		placebo_preset_combo_box->addItem(p.second, (int)p.first);
		if(current_placebo_preset == p.first)
			placebo_preset_combo_box->setCurrentIndex(placebo_preset_combo_box->count() - 1);
	}
	connect(placebo_preset_combo_box, SIGNAL(currentIndexChanged(int)), this, SLOT(PlaceboPresetSelected()));
	if (current_renderer == Renderer::PlaceboVk)
		renderer_settings_layout->addRow(tr("Placebo Preset:"), placebo_preset_combo_box);
#endif

	// Registered Consoles
	auto registered_hosts_group_box = new QGroupBox(tr("Registered Consoles"));
	other_layout->addWidget(registered_hosts_group_box);

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
	button_box->button(QDialogButtonBox::Close)->setMinimumSize(140, 40);
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

void SettingsDialog::DualSenseChanged()
{
	settings->SetDualSenseEnabled(dualsense_check_box->isChecked());
}

void SettingsDialog::ButtonsPosChanged()
{
	settings->SetButtonsByPosition(buttons_pos_check_box->isChecked());
}

void SettingsDialog::DeckOrientationChanged()
{
	settings->SetVerticalDeckEnabled(vertical_sdeck_check_box->isChecked());
}

void SettingsDialog::AutomaticConnectChanged()
{
	settings->SetAutomaticConnect(automatic_connect_check_box->isChecked());
}
#if CHIAKI_GUI_ENABLE_SPEEX
void SettingsDialog::SpeechProcessingChanged()
{
	settings->SetSpeechProcessingEnabled(speech_processing_check_box->isChecked());
}
#endif

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

void SettingsDialog::AudioInputSelected()
{
	settings->SetAudioInDevice(microphone_combo_box->currentText());
}

void SettingsDialog::HardwareDecodeEngineSelected()
{
	settings->SetHardwareDecoder(hw_decoder_combo_box->currentData().toString());
}

void SettingsDialog::UpdateHardwareDecodeEngineComboBox()
{
	hw_decoder_combo_box->setEnabled(settings->GetDecoder() == Decoder::Ffmpeg);
}

void SettingsDialog::RendererSelected()
{
	Renderer current_renderer = settings->GetRenderer();
	Renderer new_renderer = (Renderer)renderer_combo_box->currentData().toInt();

	if (current_renderer == new_renderer)
		return;

	settings->SetRenderer(new_renderer);

#ifdef CHIAKI_GUI_ENABLE_PLACEBO
	// Update codec combo box and codec setting if HDR becomes available/unavailable
	if (new_renderer == Renderer::PlaceboVk)
	{
		codec_combo_box->addItem("H265 HDR (PS5 only)", (int)CHIAKI_CODEC_H265_HDR);
		renderer_settings_layout->insertRow(1, tr("Placebo Preset:"), placebo_preset_combo_box);
	}
	else if (current_renderer == Renderer::PlaceboVk)
	{
		ChiakiCodec current_codec = settings->GetCodec();
		if (current_codec == CHIAKI_CODEC_H265_HDR)
		{
			settings->SetCodec(CHIAKI_CODEC_H265);
			codec_combo_box->setCurrentIndex(codec_combo_box->findData((int)CHIAKI_CODEC_H265));
		}
		codec_combo_box->removeItem(codec_combo_box->findData((int)CHIAKI_CODEC_H265_HDR));
		renderer_settings_layout->removeRow(placebo_preset_combo_box);
	}
#endif
}

#if CHIAKI_GUI_ENABLE_PLACEBO
void SettingsDialog::PlaceboPresetSelected()
{
	settings->SetPlaceboPreset((PlaceboPreset)placebo_preset_combo_box->currentData().toInt());
}
#endif

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
