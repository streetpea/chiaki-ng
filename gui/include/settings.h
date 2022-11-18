// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SETTINGS_H
#define CHIAKI_SETTINGS_H

#include <chiaki/session.h>

#include "host.h"

#include <QSettings>
#include <QAudioDeviceInfo>

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

enum class Decoder
{
	Ffmpeg,
	Pi
};

class Settings : public QObject
{
	Q_OBJECT

	private:
		QSettings settings;

		QMap<HostMAC, RegisteredHost> registered_hosts;
		QMap<int, ManualHost> manual_hosts;
		int manual_hosts_id_next;

		void LoadRegisteredHosts();
		void SaveRegisteredHosts();

		void LoadManualHosts();
		void SaveManualHosts();

	public:
		explicit Settings(QObject *parent = nullptr);

		bool GetDiscoveryEnabled() const		{ return settings.value("settings/auto_discovery", true).toBool(); }
		void SetDiscoveryEnabled(bool enabled)	{ settings.setValue("settings/auto_discovery", enabled); }

		bool GetLogVerbose() const 				{ return settings.value("settings/log_verbose", false).toBool(); }
		void SetLogVerbose(bool enabled)		{ settings.setValue("settings/log_verbose", enabled); }
		uint32_t GetLogLevelMask();

		bool GetDualSenseEnabled() const		{ return settings.value("settings/dualsense_enabled", false).toBool(); }
		void SetDualSenseEnabled(bool enabled)	{ settings.setValue("settings/dualsense_enabled", enabled); }

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

		ChiakiConnectVideoProfile GetVideoProfile();

		DisconnectAction GetDisconnectAction();
		void SetDisconnectAction(DisconnectAction action);

		QList<RegisteredHost> GetRegisteredHosts() const			{ return registered_hosts.values(); }
		void AddRegisteredHost(const RegisteredHost &host);
		void RemoveRegisteredHost(const HostMAC &mac);
		bool GetRegisteredHostRegistered(const HostMAC &mac) const	{ return registered_hosts.contains(mac); }
		RegisteredHost GetRegisteredHost(const HostMAC &mac) const	{ return registered_hosts[mac]; }

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
