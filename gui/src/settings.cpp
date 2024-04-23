// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <settings.h>
#include <QFile>
#include <QUrl>
#include <QKeySequence>
#include <QCoreApplication>

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

static void MigrateVideoProfile(QSettings *settings)
{
	if(settings->contains("settings/resolution"))
	{
		settings->setValue("settings/resolution_local_ps5", settings->value("settings/resolution"));
		settings->remove("settings/resolution");
	}
	if(settings->contains("settings/fps"))
	{
		settings->setValue("settings/fps_local_ps5", settings->value("settings/fps"));
		settings->remove("settings/fps");
	}
	if(settings->contains("settings/codec"))
	{
		settings->setValue("settings/codec_local_ps5", settings->value("settings/codec"));
		settings->remove("settings/codec");
	}
	if(settings->contains("settings/bitrate"))
	{
		settings->setValue("settings/bitrate_local_ps5", settings->value("settings/bitrate"));
		settings->remove("settings/bitrate");
	}
}

Settings::Settings(const QString &conf, QObject *parent) : QObject(parent),
	time_format("yyyy-MM-dd  HH:mm:ss"),
	settings(QCoreApplication::organizationName(), conf.isEmpty() ? QCoreApplication::applicationName() : QStringLiteral("%1-%2").arg(QCoreApplication::applicationName(), conf)),
	default_settings(QCoreApplication::organizationName(), QCoreApplication::applicationName())
{
	settings.setFallbacksEnabled(false);
	MigrateSettings(&settings);
	MigrateVideoProfile(&settings);
	manual_hosts_id_next = 0;
	settings.setValue("version", SETTINGS_VERSION);
	LoadRegisteredHosts();
	LoadManualHosts();
	default_settings.setFallbacksEnabled(false);
	MigrateSettings(&default_settings);
	MigrateVideoProfile(&default_settings);
	default_settings.setValue("version", SETTINGS_VERSION);
	LoadProfiles();
}

void Settings::ExportSettings(QString fileurl)
{
	// create file if it doesn't exist
	QUrl url(fileurl);
	QString filepath = url.toLocalFile();
	QFile file(filepath);
	file.open(QIODevice::ReadWrite);
	file.close();
	QSettings settings_backup(filepath, QSettings::IniFormat);
	SaveRegisteredHosts(&settings_backup);
	SaveManualHosts(&settings_backup);
    QStringList keys = settings.allKeys();
    for( QStringList::iterator i = keys.begin(); i != keys.end(); i++ )
    {
        settings_backup.setValue( *i, settings.value( *i ) );
    }
	// set hw decoder to auto since it's recommended and a platform specific decoder could cause a crash if imported on another platform
	settings_backup.setValue("settings/hw_decoder", "auto");
	settings_backup.setValue("settings/this_profile", GetCurrentProfile());
}

void Settings::ImportSettings(QString fileurl)
{
	QUrl url(fileurl);
	QString filepath = url.toLocalFile();
	QSettings settings_backup(filepath, QSettings::IniFormat);
	LoadRegisteredHosts(&settings_backup);
	LoadManualHosts(&settings_backup);
	QString profile = settings_backup.value("this_profile").toString();
	if(profile.isEmpty())
	{
		SaveRegisteredHosts();
		SaveManualHosts();
		QStringList keys = settings_backup.allKeys();
		for( QStringList::iterator i = keys.begin(); i != keys.end(); i++ )
		{
			settings.setValue( *i, settings_backup.value( *i ) );
		}
	}
	else
	{
		QSettings profile_settings(QCoreApplication::organizationName(), QStringLiteral("%1-%2").arg(QCoreApplication::applicationName(), profile));
		SaveRegisteredHosts(&profile_settings);
		SaveManualHosts(&profile_settings);
		QStringList keys = settings_backup.allKeys();
		for( QStringList::iterator i = keys.begin(); i != keys.end(); i++ )
		{
			profile_settings.setValue( *i, settings_backup.value( *i ) );
		}
		SetCurrentProfile(profile);
	}
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

static const ChiakiVideoResolutionPreset resolution_default_ps4 = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
static const ChiakiVideoResolutionPreset resolution_default_ps5_local = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
static const ChiakiVideoResolutionPreset resolution_default_ps5_remote = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;

ChiakiVideoResolutionPreset Settings::GetResolutionLocalPS4() const
{
	auto s = settings.value("settings/resolution_local_ps4", resolutions[resolution_default_ps4]).toString();
	return resolutions.key(s, resolution_default_ps4);
}

ChiakiVideoResolutionPreset Settings::GetResolutionRemotePS4() const
{
	auto s = settings.value("settings/resolution_remote_ps4", resolutions[resolution_default_ps4]).toString();
	return resolutions.key(s, resolution_default_ps4);
}

ChiakiVideoResolutionPreset Settings::GetResolutionLocalPS5() const
{
	auto s = settings.value("settings/resolution_local_ps5", resolutions[resolution_default_ps5_local]).toString();
	return resolutions.key(s, resolution_default_ps5_local);
}

ChiakiVideoResolutionPreset Settings::GetResolutionRemotePS5() const
{
	auto s = settings.value("settings/resolution_remote_ps5", resolutions[resolution_default_ps5_remote]).toString();
	return resolutions.key(s, resolution_default_ps5_remote);
}

void Settings::SetResolutionLocalPS4(ChiakiVideoResolutionPreset resolution)
{
	settings.setValue("settings/resolution_local_ps4", resolutions[resolution]);
}

void Settings::SetResolutionRemotePS4(ChiakiVideoResolutionPreset resolution)
{
	settings.setValue("settings/resolution_remote_ps4", resolutions[resolution]);
}

void Settings::SetResolutionLocalPS5(ChiakiVideoResolutionPreset resolution)
{
	settings.setValue("settings/resolution_local_ps5", resolutions[resolution]);
}

void Settings::SetResolutionRemotePS5(ChiakiVideoResolutionPreset resolution)
{
	settings.setValue("settings/resolution_remote_ps5", resolutions[resolution]);
}

static const QMap<ChiakiVideoFPSPreset, int> fps_values = {
	{ CHIAKI_VIDEO_FPS_PRESET_30, 30 },
	{ CHIAKI_VIDEO_FPS_PRESET_60, 60 }
};

static const ChiakiVideoFPSPreset fps_default = CHIAKI_VIDEO_FPS_PRESET_60;

ChiakiVideoFPSPreset Settings::GetFPSLocalPS4() const
{
	auto v = settings.value("settings/fps_local_ps4", fps_values[fps_default]).toInt();
	return fps_values.key(v, fps_default);
}

ChiakiVideoFPSPreset Settings::GetFPSRemotePS4() const
{
	auto v = settings.value("settings/fps_remote_ps4", fps_values[fps_default]).toInt();
	return fps_values.key(v, fps_default);
}

ChiakiVideoFPSPreset Settings::GetFPSLocalPS5() const
{
	auto v = settings.value("settings/fps_local_ps5", fps_values[fps_default]).toInt();
	return fps_values.key(v, fps_default);
}

ChiakiVideoFPSPreset Settings::GetFPSRemotePS5() const
{
	auto v = settings.value("settings/fps_remote_ps5", fps_values[fps_default]).toInt();
	return fps_values.key(v, fps_default);
}

void Settings::SetFPSLocalPS4(ChiakiVideoFPSPreset fps)
{
	settings.setValue("settings/fps_local_ps4", fps_values[fps]);
}

void Settings::SetFPSRemotePS4(ChiakiVideoFPSPreset fps)
{
	settings.setValue("settings/fps_remote_ps4", fps_values[fps]);
}

void Settings::SetFPSLocalPS5(ChiakiVideoFPSPreset fps)
{
	settings.setValue("settings/fps_local_ps5", fps_values[fps]);
}

void Settings::SetFPSRemotePS5(ChiakiVideoFPSPreset fps)
{
	settings.setValue("settings/fps_remote_ps5", fps_values[fps]);
}

unsigned int Settings::GetBitrateLocalPS4() const
{
	return settings.value("settings/bitrate_local_ps4", 0).toUInt();
}

unsigned int Settings::GetBitrateRemotePS4() const
{
	return settings.value("settings/bitrate_remote_ps4", 0).toUInt();
}

unsigned int Settings::GetBitrateLocalPS5() const
{
	return settings.value("settings/bitrate_local_ps5", 0).toUInt();
}

unsigned int Settings::GetBitrateRemotePS5() const
{
	return settings.value("settings/bitrate_remote_ps5", 0).toUInt();
}

void Settings::SetBitrateLocalPS4(unsigned int bitrate)
{
	settings.setValue("settings/bitrate_local_ps4", bitrate);
}

void Settings::SetBitrateRemotePS4(unsigned int bitrate)
{
	settings.setValue("settings/bitrate_remote_ps4", bitrate);
}

void Settings::SetBitrateLocalPS5(unsigned int bitrate)
{
	settings.setValue("settings/bitrate_local_ps5", bitrate);
}

void Settings::SetBitrateRemotePS5(unsigned int bitrate)
{
	settings.setValue("settings/bitrate_remote_ps5", bitrate);
}

static const QMap<ChiakiCodec, QString> codecs = {
	{ CHIAKI_CODEC_H264, "h264" },
	{ CHIAKI_CODEC_H265, "h265" },
	{ CHIAKI_CODEC_H265_HDR, "h265_hdr" }
};

static const ChiakiCodec codec_default_ps5 = CHIAKI_CODEC_H265;
static const ChiakiCodec codec_default_ps4 = CHIAKI_CODEC_H264;

ChiakiCodec Settings::GetCodecPS4() const
{
	auto v = settings.value("settings/codec_ps4", codecs[codec_default_ps4]).toString();
	auto codec = codecs.key(v, codec_default_ps4);
	return codec;
}

ChiakiCodec Settings::GetCodecLocalPS5() const
{
	auto v = settings.value("settings/codec_local_ps5", codecs[codec_default_ps5]).toString();
	auto codec = codecs.key(v, codec_default_ps5);
	return codec;
}

ChiakiCodec Settings::GetCodecRemotePS5() const
{
	auto v = settings.value("settings/codec_remote_ps5", codecs[codec_default_ps5]).toString();
	auto codec = codecs.key(v, codec_default_ps5);
	return codec;
}

void Settings::SetCodecPS4(ChiakiCodec codec)
{
	settings.setValue("settings/codec_ps4", codecs[codec]);
}

void Settings::SetCodecLocalPS5(ChiakiCodec codec)
{
	settings.setValue("settings/codec_local_ps5", codecs[codec]);
}

void Settings::SetCodecRemotePS5(ChiakiCodec codec)
{
	settings.setValue("settings/codec_remote_ps5", codecs[codec]);
}

unsigned int Settings::GetAudioBufferSizeDefault() const
{
	return 5760;
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

static const QMap<PlaceboPreset, QString> placebo_preset_values = {
	{ PlaceboPreset::Fast, "fast" },
	{ PlaceboPreset::Default, "default" },
	{ PlaceboPreset::HighQuality, "high_quality" },
	{ PlaceboPreset::Custom, "custom" }
};

PlaceboPreset Settings::GetPlaceboPreset() const
{
	auto v = settings.value("settings/placebo_preset", placebo_preset_values[PlaceboPreset::HighQuality]).toString();
	return placebo_preset_values.key(v, PlaceboPreset::Default);
}

void Settings::SetPlaceboPreset(PlaceboPreset preset)
{
	settings.setValue("settings/placebo_preset", placebo_preset_values[preset]);
}

float Settings::GetZoomFactor() const
{
	return settings.value("settings/zoom_factor", -1).toFloat();
}

void Settings::SetZoomFactor(float factor)
{
	settings.setValue("settings/zoom_factor", factor);
}

float Settings::GetPacketLossMax() const
{
	return settings.value("settings/packet_loss_max", 0.05).toFloat();
}

void Settings::SetPacketLossMax(float packet_loss_max)
{
	settings.setValue("settings/packet_loss_max", packet_loss_max);
}

static const QMap<WindowType, QString> window_type_values = {
	{ WindowType::SelectedResolution, "Selected Resolution" },
	{ WindowType::Fullscreen, "Fullscreen" },
	{ WindowType::Zoom, "Zoom" },
	{ WindowType::Stretch, "Stretch" }
};

WindowType Settings::GetWindowType() const
{
	auto v = settings.value("settings/window_type", window_type_values[WindowType::SelectedResolution]).toString();
	return window_type_values.key(v, WindowType::SelectedResolution);
}

void Settings::SetWindowType(WindowType type)
{
	settings.setValue("settings/window_type", window_type_values[type]);
}

RegisteredHost Settings::GetAutoConnectHost() const
{
	const QByteArray mac = settings.value("settings/auto_connect_mac").toByteArray();
	return GetRegisteredHost(mac.size() == 6 ? HostMAC((const uint8_t *)mac.constData()) : HostMAC());
}

void Settings::SetAutoConnectHost(const QByteArray &mac)
{
	settings.setValue("settings/auto_connect_mac", mac);
}

QString Settings::GetHardwareDecoder() const
{
	return settings.value("settings/hw_decoder", "auto").toString();
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

QString Settings::GetAudioInDevice() const
{
	return settings.value("settings/audio_in_device").toString();
}

void Settings::SetAudioOutDevice(QString device_name)
{
	settings.setValue("settings/audio_out_device", device_name);
}

void Settings::SetAudioInDevice(QString device_name)
{
	settings.setValue("settings/audio_in_device", device_name);
}

void Settings::SetAudioBufferSize(unsigned int size)
{
	settings.setValue("settings/audio_buffer_size", size);
}

unsigned int Settings::GetWifiDroppedNotif() const
{
	return settings.value("settings/wifi_dropped_notif_percent", 3).toUInt();
}

void Settings::SetWifiDroppedNotif(unsigned int percent)
{
	settings.setValue("settings/wifi_dropped_notif_percent", percent);
}

#if CHIAKI_GUI_ENABLE_SPEEX

bool Settings::GetSpeechProcessingEnabled() const
{
	return settings.value("settings/enable_speech_processing", false).toBool();
}

int Settings::GetNoiseSuppressLevel() const
{
	return settings.value("settings/noise_suppress_level", 6).toInt();
}

int Settings::GetEchoSuppressLevel() const
{
	return settings.value("settings/echo_suppress_level", 30).toInt();
}

void Settings::SetSpeechProcessingEnabled(bool enabled)
{
	settings.setValue("settings/enable_speech_processing", enabled);
}

void Settings::SetNoiseSuppressLevel(int reduceby)
{
	settings.setValue("settings/noise_suppress_level", reduceby);
}

void Settings::SetEchoSuppressLevel(int reduceby)
{
	settings.setValue("settings/echo_suppress_level", reduceby);
}
#endif

QString Settings::GetPsnAuthToken() const
{
	return settings.value("settings/psn_auth_token").toString();
}

void Settings::SetPsnAuthToken(QString auth_token)
{
	settings.setValue("settings/psn_auth_token", auth_token);
}

QString Settings::GetPsnRefreshToken() const
{
	return settings.value("settings/psn_refresh_token").toString();
}

void Settings::SetPsnRefreshToken(QString refresh_token)
{
	settings.setValue("settings/psn_refresh_token", refresh_token);
}

QString Settings::GetPsnAuthTokenExpiry() const
{
	return settings.value("settings/psn_auth_token_expiry").toString();
}

void Settings::SetPsnAuthTokenExpiry(QString expiry_date)
{
	settings.setValue("settings/psn_auth_token_expiry", expiry_date);
}

QString Settings::GetCurrentProfile() const
{
	return default_settings.value("settings/current_profile").toString();
}

void Settings::SetCurrentProfile(QString profile)
{
	if(!profile.isEmpty() && !profiles.contains(profile))
	{
		profiles.append(profile);
		SaveProfiles();
		emit ProfilesUpdated();
	}
	default_settings.setValue("settings/current_profile", profile);
	emit CurrentProfileChanged();
}

QString Settings::GetPsnAccountId() const
{
	return settings.value("settings/psn_account_id").toString();
}

void Settings::SetPsnAccountId(QString account_id)
{
	settings.setValue("settings/psn_account_id", account_id);
}

ChiakiConnectVideoProfile Settings::GetVideoProfileLocalPS4()
{
	ChiakiConnectVideoProfile profile = {};
	chiaki_connect_video_profile_preset(&profile, GetResolutionLocalPS4(), GetFPSLocalPS4());
	unsigned int bitrate = GetBitrateLocalPS4();
	if(bitrate)
		profile.bitrate = bitrate;
	profile.codec = GetCodecPS4();
	return profile;
}

ChiakiConnectVideoProfile Settings::GetVideoProfileRemotePS4()
{
	ChiakiConnectVideoProfile profile = {};
	chiaki_connect_video_profile_preset(&profile, GetResolutionRemotePS4(), GetFPSRemotePS4());
	unsigned int bitrate = GetBitrateRemotePS4();
	if(bitrate)
		profile.bitrate = bitrate;
	profile.codec = GetCodecPS4();
	return profile;
}

ChiakiConnectVideoProfile Settings::GetVideoProfileLocalPS5()
{
	ChiakiConnectVideoProfile profile = {};
	chiaki_connect_video_profile_preset(&profile, GetResolutionLocalPS5(), GetFPSLocalPS5());
	unsigned int bitrate = GetBitrateLocalPS5();
	if(bitrate)
		profile.bitrate = bitrate;
	profile.codec = GetCodecLocalPS5();
	return profile;
}

ChiakiConnectVideoProfile Settings::GetVideoProfileRemotePS5()
{
	ChiakiConnectVideoProfile profile = {};
	chiaki_connect_video_profile_preset(&profile, GetResolutionRemotePS5(), GetFPSRemotePS5());
	unsigned int bitrate = GetBitrateRemotePS5();
	if(bitrate)
		profile.bitrate = bitrate;
	profile.codec = GetCodecRemotePS5();
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

static const QMap<SuspendAction, QString> suspend_action_values = {
	{ SuspendAction::Sleep, "sleep" },
	{ SuspendAction::Nothing, "nothing" },
};

static const SuspendAction suspend_action_default = SuspendAction::Nothing;

SuspendAction Settings::GetSuspendAction()
{
	auto v = settings.value("settings/suspend_action", suspend_action_values[suspend_action_default]).toString();
	return suspend_action_values.key(v, suspend_action_default);
}

void Settings::SetSuspendAction(SuspendAction action)
{
	settings.setValue("settings/suspend_action", suspend_action_values[action]);
}

void Settings::LoadProfiles()
{
	profiles.clear();
	int count = default_settings.beginReadArray("profiles");
	for(int i = 0; i<count; i++)
	{
		default_settings.setArrayIndex(i);
		profiles.append(default_settings.value("settings/profile_name").toString());
	}
	default_settings.endArray();
}

void Settings::SaveProfiles()
{
	default_settings.beginWriteArray("profiles");
	for(int i = 0; i<profiles.size(); i++)
	{
		default_settings.setArrayIndex(i);
		default_settings.setValue("settings/profile_name", profiles[i]);
	}
	default_settings.endArray();
}

void Settings::DeleteProfile(QString profile)
{
	QSettings delete_profile(QCoreApplication::organizationName(), QStringLiteral("%1-%2").arg(QCoreApplication::applicationName(), profile));
	registered_hosts.clear();
	manual_hosts.clear();
	SaveRegisteredHosts(&delete_profile);
	SaveManualHosts(&delete_profile);
	delete_profile.remove("settings");
	profiles.removeOne(profile);
	SaveProfiles();
	emit ProfilesUpdated();
	LoadRegisteredHosts();
	LoadManualHosts();
}

void Settings::LoadRegisteredHosts(QSettings *qsettings)
{
	if(!qsettings)
		qsettings = &settings;
	registered_hosts.clear();
	nickname_registered_hosts.clear();
	ps4s_registered = 0;

	int count = qsettings->beginReadArray("registered_hosts");
	for(int i=0; i<count; i++)
	{
		qsettings->setArrayIndex(i);
		RegisteredHost host = RegisteredHost::LoadFromSettings(qsettings);
		registered_hosts[host.GetServerMAC()] = host;
		nickname_registered_hosts[host.GetServerNickname()] = host;
		if(!chiaki_target_is_ps5(host.GetTarget()))
			ps4s_registered++;
	}
	qsettings->endArray();
}

void Settings::SaveRegisteredHosts(QSettings *qsettings)
{
	if(!qsettings)
		qsettings = &settings;
	qsettings->beginWriteArray("registered_hosts");
	int i=0;
	for(const auto &host : registered_hosts)
	{
		qsettings->setArrayIndex(i);
		host.SaveToSettings(qsettings);
		i++;
	}
	qsettings->endArray();
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

void Settings::LoadManualHosts(QSettings *qsettings)
{
	if(!qsettings)
		qsettings = &settings;
	manual_hosts.clear();

	int count = qsettings->beginReadArray("manual_hosts");
	for(int i=0; i<count; i++)
	{
		qsettings->setArrayIndex(i);
		ManualHost host = ManualHost::LoadFromSettings(qsettings);
		if(host.GetID() < 0)
			continue;
		if(manual_hosts_id_next <= host.GetID())
			manual_hosts_id_next = host.GetID() + 1;
		manual_hosts[host.GetID()] = host;
	}
	qsettings->endArray();
}

void Settings::SaveManualHosts(QSettings *qsettings)
{
	if(!qsettings)
		qsettings = &settings;
	qsettings->beginWriteArray("manual_hosts");
	int i=0;
	for(const auto &host : manual_hosts)
	{
		qsettings->setArrayIndex(i);
		host.SaveToSettings(qsettings);
		i++;
	}
	qsettings->endArray();
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
			result[static_cast<int>(chiaki_button)] = QKeySequence(settings.value("keymap/" + button_name).toString())[0].key();
	}

	return result;
}

void Settings::ClearKeyMapping()
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
			settings.remove("keymap/" + button_name);
	}
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
