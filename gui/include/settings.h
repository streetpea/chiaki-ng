// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SETTINGS_H
#define CHIAKI_SETTINGS_H

#include <chiaki/session.h>

#include "host.h"

#include <QSettings>
#include <QList>

enum class ControllerButtonExt
{
	// must not overlap with ChiakiControllerButton and ChiakiControllerAnalogButton
	ANALOG_STICK_LEFT_X_UP = (1 << 18),
	ANALOG_STICK_LEFT_X_DOWN = (1 << 19),
	ANALOG_STICK_LEFT_Y_UP = (1 << 20),
	ANALOG_STICK_LEFT_Y_DOWN = (1 << 21),
	ANALOG_STICK_RIGHT_X_UP = (1 << 22),
	ANALOG_STICK_RIGHT_X_DOWN = (1 << 23),
	ANALOG_STICK_RIGHT_Y_UP = (1 << 24),
	ANALOG_STICK_RIGHT_Y_DOWN = (1 << 25),
};

enum class DisconnectAction
{
	AlwaysNothing,
	AlwaysSleep,
	Ask
};

enum class SuspendAction
{
	Nothing,
	Sleep,
};

enum class Decoder
{
	Ffmpeg,
	Pi
};

enum class PlaceboPreset {
	Fast,
	Default,
	HighQuality
};

enum class WindowType {
	SelectedResolution,
	Fullscreen,
	Zoom,
	Stretch
};

class Settings : public QObject
{
	Q_OBJECT

	private:
		QSettings settings;
		QString time_format;

		QMap<HostMAC, RegisteredHost> registered_hosts;
		QMap<QString, RegisteredHost> nickname_registered_hosts;
		size_t ps4s_registered;
		QMap<int, ManualHost> manual_hosts;
		int manual_hosts_id_next;

		void LoadRegisteredHosts();
		void SaveRegisteredHosts();

		void LoadManualHosts();
		void SaveManualHosts();

	public:
		explicit Settings(const QString &conf, QObject *parent = nullptr);

		bool GetDiscoveryEnabled() const		{ return settings.value("settings/auto_discovery", true).toBool(); }
		void SetDiscoveryEnabled(bool enabled)	{ settings.setValue("settings/auto_discovery", enabled); }

		bool GetLogVerbose() const 				{ return settings.value("settings/log_verbose", false).toBool(); }
		void SetLogVerbose(bool enabled)		{ settings.setValue("settings/log_verbose", enabled); }
		uint32_t GetLogLevelMask();

		bool GetDualSenseEnabled() const		{ return settings.value("settings/dualsense_enabled", true).toBool(); }
		void SetDualSenseEnabled(bool enabled)	{ settings.setValue("settings/dualsense_enabled", enabled); }

		bool GetButtonsByPosition() const 		{ return settings.value("settings/buttons_by_pos", false).toBool(); }
		void SetButtonsByPosition(bool enabled) { settings.setValue("settings/buttons_by_pos", enabled); }

		bool GetStartMicUnmuted() const          { return settings.value("settings/start_mic_unmuted", false).toBool(); }
		void SetStartMicUnmuted(bool unmuted) { return settings.setValue("settings/start_mic_unmuted", unmuted); }

#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
		bool GetVerticalDeckEnabled() const       { return settings.value("settings/gyro_inverted", false).toBool(); }
		void SetVerticalDeckEnabled(bool enabled) { settings.setValue("settings/gyro_inverted", enabled); }

		bool GetSteamDeckHapticsEnabled() const   { return settings.value("settings/steamdeck_haptics", false).toBool(); }
		void SetSteamDeckHapticsEnabled(bool enabled) { settings.setValue("settings/steamdeck_haptics", enabled); }
#endif

		bool GetAutomaticConnect() const         { return settings.value("settings/automatic_connect", false).toBool(); }
		void SetAutomaticConnect(bool autoconnect)    { settings.setValue("settings/automatic_connect", autoconnect); }

		ChiakiVideoResolutionPreset GetResolution() const;
		void SetResolution(ChiakiVideoResolutionPreset resolution);

		/**
		 * @return 0 if set to "automatic"
		 */
		ChiakiVideoFPSPreset GetFPS() const;
		void SetFPS(ChiakiVideoFPSPreset fps);

		unsigned int GetBitrate() const;
		void SetBitrate(unsigned int bitrate);

		ChiakiCodec GetCodec() const;
		void SetCodec(ChiakiCodec codec);

		Decoder GetDecoder() const;
		void SetDecoder(Decoder decoder);

		QString GetHardwareDecoder() const;
		void SetHardwareDecoder(const QString &hw_decoder);

		WindowType GetWindowType() const;
		void SetWindowType(WindowType type);

		PlaceboPreset GetPlaceboPreset() const;
		void SetPlaceboPreset(PlaceboPreset preset);

		float GetZoomFactor() const;
		void SetZoomFactor(float factor);

		RegisteredHost GetAutoConnectHost() const;
		void SetAutoConnectHost(const QByteArray &mac);

		unsigned int GetAudioBufferSizeDefault() const;

		/**
		 * @return 0 if set to "automatic"
		 */
		unsigned int GetAudioBufferSizeRaw() const;

		/**
		 * @return actual size to be used, default value if GetAudioBufferSizeRaw() would return 0
		 */
		unsigned int GetAudioBufferSize() const;
		void SetAudioBufferSize(unsigned int size);
		
		QString GetAudioOutDevice() const;
		void SetAudioOutDevice(QString device_name);

		QString GetAudioInDevice() const;
		void SetAudioInDevice(QString device_name);

		uint GetWifiDroppedNotif() const;
		void SetWifiDroppedNotif(uint percent);

		QString GetPsnAuthToken() const;
		void SetPsnAuthToken(QString auth_token);

		QString GetPsnRefreshToken() const;
		void SetPsnRefreshToken(QString refresh_token);

		QString GetPsnAuthTokenExpiry() const;
		void SetPsnAuthTokenExpiry(QString expiry_date);

		QString GetPsnAccountId() const;
		void SetPsnAccountId(QString account_id);

		QString GetTimeFormat() const     { return time_format; }
		void ClearKeyMapping();

#if CHIAKI_GUI_ENABLE_SPEEX
		bool GetSpeechProcessingEnabled() const;
		int GetNoiseSuppressLevel() const;
		int GetEchoSuppressLevel() const;

		void SetSpeechProcessingEnabled(bool enabled);
		void SetNoiseSuppressLevel(int reduceby);
		void SetEchoSuppressLevel(int reduceby);
#endif

		ChiakiConnectVideoProfile GetVideoProfile();

		DisconnectAction GetDisconnectAction();
		void SetDisconnectAction(DisconnectAction action);

		SuspendAction GetSuspendAction();
		void SetSuspendAction(SuspendAction action);

		QList<RegisteredHost> GetRegisteredHosts() const			{ return registered_hosts.values(); }
		void AddRegisteredHost(const RegisteredHost &host);
		void RemoveRegisteredHost(const HostMAC &mac);
		bool GetRegisteredHostRegistered(const HostMAC &mac) const	{ return registered_hosts.contains(mac); }
		RegisteredHost GetRegisteredHost(const HostMAC &mac) const	{ return registered_hosts[mac]; }
		bool GetNicknameRegisteredHostRegistered(const QString &nickname) const { return nickname_registered_hosts.contains(nickname); }
		RegisteredHost GetNicknameRegisteredHost(const QString &nickname) const { return nickname_registered_hosts[nickname]; }
		size_t GetPS4RegisteredHostsRegistered() const { return ps4s_registered; }

		QList<ManualHost> GetManualHosts() const 					{ return manual_hosts.values(); }
		int SetManualHost(const ManualHost &host);
		void RemoveManualHost(int id);
		bool GetManualHostExists(int id)							{ return manual_hosts.contains(id); }
		ManualHost GetManualHost(int id) const						{ return manual_hosts[id]; }

		static QString GetChiakiControllerButtonName(int);
		void SetControllerButtonMapping(int, Qt::Key);
		QMap<int, Qt::Key> GetControllerMapping();
		QMap<Qt::Key, int> GetControllerMappingForDecoding();

	signals:
		void RegisteredHostsUpdated();
		void ManualHostsUpdated();
};

#endif // CHIAKI_SETTINGS_H
