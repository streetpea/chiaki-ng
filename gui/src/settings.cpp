// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <settings.h>
#include <QKeySequence>

#include <chiaki/config.h>

#define SETTINGS_VERSION 2

static void MigrateSettingsTo2(QSettings *settings)
{
	QList<QMap<QString, QVariant>> hosts;
	int count = settings->beginReadArray("registered_hosts");
	for(int i=0; i<count; i++)
	{
		settings->setArrayIndex(i);
		QMap<QString, QVariant> host;
		for(QString k : settings->allKeys())
			host[k] = settings->value(k);
		hosts.append(host);
	}
	settings->endArray();
	settings->remove("registered_hosts");
	settings->beginWriteArray("registered_hosts");
	int i=0;
	for(const auto &host : hosts)
	{
		settings->setArrayIndex(i);
		settings->setValue("target", (int)CHIAKI_TARGET_PS4_10);
		for(auto it = host.constBegin(); it != host.constEnd(); it++)
		{
			QString k = it.key();
			if(k == "ps4_nickname")
				k = "server_nickname";
			else if(k == "ps4_mac")
				k = "server_mac";
			settings->setValue(k, it.value());
		}
		i++;
	}
	settings->endArray();
	QString hw_decoder = settings->value("settings/hw_decode_engine").toString();
	settings->remove("settings/hw_decode_engine");
	if(hw_decoder != "none")
		settings->setValue("settings/hw_decoder", hw_decoder);
}

static void MigrateSettings(QSettings *settings)
{
	int version_prev = settings->value("version", 0).toInt();
	if(version_prev < 1)
		return;
	if(version_prev > SETTINGS_VERSION)
	{
		CHIAKI_LOGE(NULL, "Settings version %d is higher than application one (%d)", version_prev, SETTINGS_VERSION);
		return;
	}
	while(version_prev < SETTINGS_VERSION)
	{
		version_prev++;
		switch(version_prev)
		{
			case 2:
				CHIAKI_LOGI(NULL, "Migrating settings to 2");
				MigrateSettingsTo2(settings);
				break;
			default:
				break;
		}
	}
}

Settings::Settings(QObject *parent) : QObject(parent)
{
	MigrateSettings(&settings);
	manual_hosts_id_next = 0;
	settings.setValue("version", SETTINGS_VERSION);
	LoadRegisteredHosts();
	LoadManualHosts();
}

uint32_t Settings::GetLogLevelMask()
{
	uint32_t mask = CHIAKI_LOG_ALL;
	if(!GetLogVerbose())
		mask &= ~CHIAKI_LOG_VERBOSE;
	return mask;
}

static const QMap<ChiakiVideoResolutionPreset, QString> resolutions = {
	{ CHIAKI_VIDEO_RESOLUTION_PRESET_360p, "360p" },
	{ CHIAKI_VIDEO_RESOLUTION_PRESET_540p, "540p" },
	{ CHIAKI_VIDEO_RESOLUTION_PRESET_720p, "720p" },
	{ CHIAKI_VIDEO_RESOLUTION_PRESET_1080p, "1080p" }
};

static const ChiakiVideoResolutionPreset resolution_default = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;

ChiakiVideoResolutionPreset Settings::GetResolution() const
{
	auto s = settings.value("settings/resolution", resolutions[resolution_default]).toString();
	return resolutions.key(s, resolution_default);
}

void Settings::SetResolution(ChiakiVideoResolutionPreset resolution)
{
	settings.setValue("settings/resolution", resolutions[resolution]);
}

static const QMap<ChiakiVideoFPSPreset, int> fps_values = {
	{ CHIAKI_VIDEO_FPS_PRESET_30, 30 },
	{ CHIAKI_VIDEO_FPS_PRESET_60, 60 }
};

static const ChiakiVideoFPSPreset fps_default = CHIAKI_VIDEO_FPS_PRESET_60;

ChiakiVideoFPSPreset Settings::GetFPS() const
{
	auto v = settings.value("settings/fps", fps_values[fps_default]).toInt();
	return fps_values.key(v, fps_default);
}

void Settings::SetFPS(ChiakiVideoFPSPreset fps)
{
	settings.setValue("settings/fps", fps_values[fps]);
}

unsigned int Settings::GetBitrate() const
{
	return settings.value("settings/bitrate", 0).toUInt();
}

void Settings::SetBitrate(unsigned int bitrate)
{
	settings.setValue("settings/bitrate", bitrate);
}

static const QMap<ChiakiCodec, QString> codecs = {
	{ CHIAKI_CODEC_H264, "h264" },
	{ CHIAKI_CODEC_H265, "h265" }
};

static const ChiakiCodec codec_default = CHIAKI_CODEC_H265;

ChiakiCodec Settings::GetCodec() const
{
	auto v = settings.value("settings/codec", codecs[codec_default]).toString();
	return codecs.key(v, codec_default);
}

void Settings::SetCodec(ChiakiCodec codec)
{
	settings.setValue("settings/codec", codecs[codec]);
}

unsigned int Settings::GetAudioBufferSizeDefault() const
{
	return 9600;
}

unsigned int Settings::GetAudioBufferSizeRaw() const
{
	return settings.value("settings/audio_buffer_size", 0).toUInt();
}

static const QMap<Decoder, QString> decoder_values = {
	{ Decoder::Ffmpeg, "ffmpeg" },
	{ Decoder::Pi, "pi" }
};

static const Decoder decoder_default = Decoder::Pi;

Decoder Settings::GetDecoder() const
{
#if CHIAKI_LIB_ENABLE_PI_DECODER
	auto v = settings.value("settings/decoder", decoder_values[decoder_default]).toString();
	return decoder_values.key(v, decoder_default);
#else
	return Decoder::Ffmpeg;
#endif
}

void Settings::SetDecoder(Decoder decoder)
{
	settings.setValue("settings/decoder", decoder_values[decoder]);
}

QString Settings::GetHardwareDecoder() const
{
	return settings.value("settings/hw_decoder").toString();
}

void Settings::SetHardwareDecoder(const QString &hw_decoder)
{
	settings.setValue("settings/hw_decoder", hw_decoder);
}

unsigned int Settings::GetAudioBufferSize() const
{
	unsigned int v = GetAudioBufferSizeRaw();
	return v ? v : GetAudioBufferSizeDefault();
}

QString Settings::GetAudioOutDevice() const
{
	return settings.value("settings/audio_out_device").toString();
}

void Settings::SetAudioOutDevice(QString device_name)
{
	settings.setValue("settings/audio_out_device", device_name);
}

void Settings::SetAudioBufferSize(unsigned int size)
{
	settings.setValue("settings/audio_buffer_size", size);
}

ChiakiConnectVideoProfile Settings::GetVideoProfile()
{
	ChiakiConnectVideoProfile profile = {};
	chiaki_connect_video_profile_preset(&profile, GetResolution(), GetFPS());
	unsigned int bitrate = GetBitrate();
	if(bitrate)
		profile.bitrate = bitrate;
	profile.codec = GetCodec();
	return profile;
}

static const QMap<DisconnectAction, QString> disconnect_action_values = {
	{ DisconnectAction::Ask, "ask" },
	{ DisconnectAction::AlwaysNothing, "nothing" },
	{ DisconnectAction::AlwaysSleep, "sleep" }
};

static const DisconnectAction disconnect_action_default = DisconnectAction::Ask;

DisconnectAction Settings::GetDisconnectAction()
{
	auto v = settings.value("settings/disconnect_action", disconnect_action_values[disconnect_action_default]).toString();
	return disconnect_action_values.key(v, disconnect_action_default);
}

void Settings::SetDisconnectAction(DisconnectAction action)
{
	settings.setValue("settings/disconnect_action", disconnect_action_values[action]);
}

void Settings::LoadRegisteredHosts()
{
	registered_hosts.clear();

	int count = settings.beginReadArray("registered_hosts");
	for(int i=0; i<count; i++)
	{
		settings.setArrayIndex(i);
		RegisteredHost host = RegisteredHost::LoadFromSettings(&settings);
		registered_hosts[host.GetServerMAC()] = host;
	}
	settings.endArray();
}

void Settings::SaveRegisteredHosts()
{
	settings.beginWriteArray("registered_hosts");
	int i=0;
	for(const auto &host : registered_hosts)
	{
		settings.setArrayIndex(i);
		host.SaveToSettings(&settings);
		i++;
	}
	settings.endArray();
}

void Settings::AddRegisteredHost(const RegisteredHost &host)
{
	registered_hosts[host.GetServerMAC()] = host;
	SaveRegisteredHosts();
	emit RegisteredHostsUpdated();
}

void Settings::RemoveRegisteredHost(const HostMAC &mac)
{
	if(!registered_hosts.contains(mac))
		return;
	registered_hosts.remove(mac);
	SaveRegisteredHosts();
	emit RegisteredHostsUpdated();
}

void Settings::LoadManualHosts()
{
	manual_hosts.clear();

	int count = settings.beginReadArray("manual_hosts");
	for(int i=0; i<count; i++)
	{
		settings.setArrayIndex(i);
		ManualHost host = ManualHost::LoadFromSettings(&settings);
		if(host.GetID() < 0)
			continue;
		if(manual_hosts_id_next <= host.GetID())
			manual_hosts_id_next = host.GetID() + 1;
		manual_hosts[host.GetID()] = host;
	}
	settings.endArray();

}

void Settings::SaveManualHosts()
{
	settings.beginWriteArray("manual_hosts");
	int i=0;
	for(const auto &host : manual_hosts)
	{
		settings.setArrayIndex(i);
		host.SaveToSettings(&settings);
		i++;
	}
	settings.endArray();
}

int Settings::SetManualHost(const ManualHost &host)
{
	int id = host.GetID();
	if(id < 0)
		id = manual_hosts_id_next++;
	ManualHost save_host(id, host);
	manual_hosts[id] = save_host;
	SaveManualHosts();
	emit ManualHostsUpdated();
	return id;
}

void Settings::RemoveManualHost(int id)
{
	manual_hosts.remove(id);
	SaveManualHosts();
	emit ManualHostsUpdated();
}

QString Settings::GetChiakiControllerButtonName(int button)
{
	switch(button)
	{
		case CHIAKI_CONTROLLER_BUTTON_CROSS      : return tr("Cross");
		case CHIAKI_CONTROLLER_BUTTON_MOON       : return tr("Moon");
		case CHIAKI_CONTROLLER_BUTTON_BOX        : return tr("Box");
		case CHIAKI_CONTROLLER_BUTTON_PYRAMID    : return tr("Pyramid");
		case CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT  : return tr("D-Pad Left");
		case CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT : return tr("D-Pad Right");
		case CHIAKI_CONTROLLER_BUTTON_DPAD_UP    : return tr("D-Pad Up");
		case CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN  : return tr("D-Pad Down");
		case CHIAKI_CONTROLLER_BUTTON_L1         : return tr("L1");
		case CHIAKI_CONTROLLER_BUTTON_R1         : return tr("R1");
		case CHIAKI_CONTROLLER_BUTTON_L3         : return tr("L3");
		case CHIAKI_CONTROLLER_BUTTON_R3         : return tr("R3");
		case CHIAKI_CONTROLLER_BUTTON_OPTIONS    : return tr("Options");
		case CHIAKI_CONTROLLER_BUTTON_SHARE      : return tr("Share");
		case CHIAKI_CONTROLLER_BUTTON_TOUCHPAD   : return tr("Touchpad");
		case CHIAKI_CONTROLLER_BUTTON_PS         : return tr("PS");
		case CHIAKI_CONTROLLER_ANALOG_BUTTON_L2  : return tr("L2");
		case CHIAKI_CONTROLLER_ANALOG_BUTTON_R2  : return tr("R2");
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X_UP)    : return tr("Left Stick Right");
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y_UP)    : return tr("Left Stick Up");
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X_UP)   : return tr("Right Stick Right");
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y_UP)   : return tr("Right Stick Up");
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X_DOWN)  : return tr("Left Stick Left");
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y_DOWN)  : return tr("Left Stick Down");
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X_DOWN) : return tr("Right Stick Left");
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y_DOWN) : return tr("Right Stick Down");
		default: return "Unknown";
	}
}

void Settings::SetControllerButtonMapping(int chiaki_button, Qt::Key key)
{
	auto button_name = GetChiakiControllerButtonName(chiaki_button).replace(' ', '_').toLower();
	settings.setValue("keymap/" + button_name, QKeySequence(key).toString());
}

QMap<int, Qt::Key> Settings::GetControllerMapping()
{
	// Initialize with default values
	QMap<int, Qt::Key> result =
	{
		{CHIAKI_CONTROLLER_BUTTON_CROSS     , Qt::Key::Key_Return},
		{CHIAKI_CONTROLLER_BUTTON_MOON      , Qt::Key::Key_Backspace},
		{CHIAKI_CONTROLLER_BUTTON_BOX       , Qt::Key::Key_Backslash},
		{CHIAKI_CONTROLLER_BUTTON_PYRAMID   , Qt::Key::Key_C},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT , Qt::Key::Key_Left},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT, Qt::Key::Key_Right},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_UP   , Qt::Key::Key_Up},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN , Qt::Key::Key_Down},
		{CHIAKI_CONTROLLER_BUTTON_L1        , Qt::Key::Key_2},
		{CHIAKI_CONTROLLER_BUTTON_R1        , Qt::Key::Key_3},
		{CHIAKI_CONTROLLER_BUTTON_L3        , Qt::Key::Key_5},
		{CHIAKI_CONTROLLER_BUTTON_R3        , Qt::Key::Key_6},
		{CHIAKI_CONTROLLER_BUTTON_OPTIONS   , Qt::Key::Key_O},
		{CHIAKI_CONTROLLER_BUTTON_SHARE     , Qt::Key::Key_F},
		{CHIAKI_CONTROLLER_BUTTON_TOUCHPAD  , Qt::Key::Key_T},
		{CHIAKI_CONTROLLER_BUTTON_PS        , Qt::Key::Key_Escape},
		{CHIAKI_CONTROLLER_ANALOG_BUTTON_L2 , Qt::Key::Key_1},
		{CHIAKI_CONTROLLER_ANALOG_BUTTON_R2 , Qt::Key::Key_4},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X_UP)   , Qt::Key::Key_BracketRight},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X_DOWN) , Qt::Key::Key_BracketLeft},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y_UP)   , Qt::Key::Key_Insert},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y_DOWN) , Qt::Key::Key_Delete},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X_UP)  , Qt::Key::Key_Equal},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X_DOWN), Qt::Key::Key_Minus},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y_UP)  , Qt::Key::Key_PageUp},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y_DOWN), Qt::Key::Key_PageDown}
	};

	// Then fill in from settings
	auto chiaki_buttons = result.keys();
	for(auto chiaki_button : chiaki_buttons)
	{
		auto button_name = GetChiakiControllerButtonName(chiaki_button).replace(' ', '_').toLower();
		if(settings.contains("keymap/" + button_name))
			result[static_cast<int>(chiaki_button)] = Qt::Key(QKeySequence(settings.value("keymap/" + button_name).toString())[0]);
	}

	return result;
}

QMap<Qt::Key, int> Settings::GetControllerMappingForDecoding()
{
	auto map = GetControllerMapping();
	QMap<Qt::Key, int> result;
	for(auto it = map.begin(); it != map.end(); ++it)
	{
		result[it.value()] = it.key();
	}
	return result;
}
