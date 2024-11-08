// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SETTINGS_H
#define CHIAKI_SETTINGS_H

#include <chiaki/session.h>

#include "host.h"

#include <QSettings>
#include <QList>
#include <QMap>

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
	ANALOG_STICK_LEFT_X = (1 << 26),
	ANALOG_STICK_LEFT_Y = (1 << 27),
	ANALOG_STICK_RIGHT_X = (1 << 28),
	ANALOG_STICK_RIGHT_Y = (1 << 29),
	MISC1 = (1 << 30),
};

enum class RumbleHapticsIntensity
{
	Off,
	VeryWeak,
	Weak,
	Normal,
	Strong,
	VeryStrong
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
	HighQuality,
	Custom
};

enum class WindowType {
	SelectedResolution,
	CustomResolution,
	Fullscreen,
	Zoom,
	Stretch
};

enum class PlaceboUpscaler {
	None,
	Nearest,
	Bilinear,
	Oversample,
	Bicubic,
	Gaussian,
	CatmullRom,
	Lanczos,
	EwaLanczos,
	EwaLanczosSharp,
	EwaLanczos4Sharpest
};

enum class PlaceboDownscaler {
	None,
	Box,
	Hermite,
	Bilinear,
	Bicubic,
	Gaussian,
	CatmullRom,
	Mitchell,
	Lanczos
};

enum class PlaceboFrameMixer {
	None,
	Oversample,
	Hermite,
	Linear,
	Cubic
};

enum class PlaceboDebandPreset {
	None,
	Default
};

enum class PlaceboSigmoidPreset {
	None,
	Default
};

enum class PlaceboColorAdjustmentPreset {
	None,
	Neutral
};

enum class PlaceboPeakDetectionPreset {
	None,
	Default,
	HighQuality
};

enum class PlaceboColorMappingPreset {
	None,
	Default,
	HighQuality
};

enum class PlaceboGamutMappingFunction {
	Clip,
	Perceptual,
	SoftClip,
	Relative,
	Saturation,
	Absolute,
	Desaturate,
	Darken,
	Highlight,
	Linear
};

enum class PlaceboToneMappingFunction {
	Clip,
	Spline,
	St209440,
	St209410,
	Bt2390,
	Bt2446a,
	Reinhard,
	Mobius,
	Hable,
	Gamma,
	Linear,
	LinearLight
};

enum class PlaceboToneMappingMetadata {
	Any,
	None,
	Hdr10,
	Hdr10Plus,
	CieY
};
class Settings : public QObject
{
	Q_OBJECT

	private:
		QSettings settings;
		QSettings default_settings;
		QSettings placebo_settings;
		QString time_format;
		QMap<HostMAC, HiddenHost> hidden_hosts;

		QMap<HostMAC, RegisteredHost> registered_hosts;
		QMap<QString, RegisteredHost> nickname_registered_hosts;
		QMap<QString, QString> controller_mappings;
		QList<QString> profiles;
		size_t ps4s_registered;
		QMap<int, ManualHost> manual_hosts;
		int manual_hosts_id_next;

		void LoadRegisteredHosts(QSettings *qsettings = nullptr);
		void SaveRegisteredHosts(QSettings *qsettings = nullptr);

		void LoadHiddenHosts(QSettings *qsettings = nullptr);
		void SaveHiddenHosts(QSettings *qsettings = nullptr);

		void LoadManualHosts(QSettings *qsettings = nullptr);
		void SaveManualHosts(QSettings *qsettings = nullptr);

		void LoadControllerMappings(QSettings *qsettings = nullptr);
		void SaveControllerMappings(QSettings *qsettings = nullptr);

		void LoadProfiles();
		void SaveProfiles();

	public:
		explicit Settings(const QString &conf, QObject *parent = nullptr);

		void ExportSettings(QString fileurl);
		void ImportSettings(QString fileurl);

		void ExportPlaceboSettings(QString fileurl);
		void ImportPlaceboSettings(QString fileurl);

		QMap<QString, QString> GetPlaceboValues();

		bool GetDiscoveryEnabled() const		{ return settings.value("settings/auto_discovery", true).toBool(); }
		void SetDiscoveryEnabled(bool enabled)	{ settings.setValue("settings/auto_discovery", enabled); }

		bool GetRemotePlayAsk() const           { return settings.value("settings/remote_play_ask", true).toBool(); }
		void SetRemotePlayAsk(bool asked)       { settings.setValue("settings/remote_play_ask", asked); }
		bool GetLogVerbose() const 				{ return settings.value("settings/log_verbose", false).toBool(); }
		void SetLogVerbose(bool enabled)		{ settings.setValue("settings/log_verbose", enabled); }
		uint32_t GetLogLevelMask();

		RumbleHapticsIntensity GetRumbleHapticsIntensity() const;
		void SetRumbleHapticsIntensity(RumbleHapticsIntensity intensity);

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

		bool GetFullscreenDoubleClickEnabled() const	   { return settings.value("settings/fullscreen_doubleclick", false).toBool(); }
		void SetFullscreenDoubleClickEnabled(bool enabled) { settings.setValue("settings/fullscreen_doubleclick", enabled); }

		ChiakiVideoResolutionPreset GetResolutionLocalPS4() const;
		ChiakiVideoResolutionPreset GetResolutionRemotePS4() const;
		ChiakiVideoResolutionPreset GetResolutionLocalPS5() const;
		ChiakiVideoResolutionPreset GetResolutionRemotePS5() const;
		void SetResolutionLocalPS4(ChiakiVideoResolutionPreset resolution);
		void SetResolutionRemotePS4(ChiakiVideoResolutionPreset resolution);
		void SetResolutionLocalPS5(ChiakiVideoResolutionPreset resolution);
		void SetResolutionRemotePS5(ChiakiVideoResolutionPreset resolution);

		/**
		 * @return 0 if set to "automatic"
		 */
		ChiakiVideoFPSPreset GetFPSLocalPS4() const;
		ChiakiVideoFPSPreset GetFPSRemotePS4() const;
		ChiakiVideoFPSPreset GetFPSLocalPS5() const;
		ChiakiVideoFPSPreset GetFPSRemotePS5() const;
		void SetFPSLocalPS4(ChiakiVideoFPSPreset fps);
		void SetFPSRemotePS4(ChiakiVideoFPSPreset fps);
		void SetFPSLocalPS5(ChiakiVideoFPSPreset fps);
		void SetFPSRemotePS5(ChiakiVideoFPSPreset fps);

		unsigned int GetBitrateLocalPS4() const;
		unsigned int GetBitrateRemotePS4() const;
		unsigned int GetBitrateLocalPS5() const;
		unsigned int GetBitrateRemotePS5() const;
		void SetBitrateLocalPS4(unsigned int bitrate);
		void SetBitrateRemotePS4(unsigned int bitrate);
		void SetBitrateLocalPS5(unsigned int bitrate);
		void SetBitrateRemotePS5(unsigned int bitrate);

		ChiakiCodec GetCodecPS4() const;
		ChiakiCodec GetCodecLocalPS5() const;
		ChiakiCodec GetCodecRemotePS5() const;
		void SetCodecPS4(ChiakiCodec codec);
		void SetCodecLocalPS5(ChiakiCodec codec);
		void SetCodecRemotePS5(ChiakiCodec codec);

		Decoder GetDecoder() const;
		void SetDecoder(Decoder decoder);

		QString GetHardwareDecoder() const;
		void SetHardwareDecoder(const QString &hw_decoder);

		WindowType GetWindowType() const;
		void SetWindowType(WindowType type);

		uint GetCustomResolutionWidth() const;
		void SetCustomResolutionWidth(uint width);

		uint GetCustomResolutionHeight() const;
		void SetCustomResolutionHeight(uint length);

		PlaceboPreset GetPlaceboPreset() const;
		void SetPlaceboPreset(PlaceboPreset preset);

		float GetZoomFactor() const;
		void SetZoomFactor(float factor);

		float GetPacketLossMax() const;
		void SetPacketLossMax(float factor);

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

		QString GetCurrentProfile() const;
		void SetCurrentProfile(QString profile);

		bool GetDpadTouchEnabled() const;
		void SetDpadTouchEnabled(bool enabled);

		uint16_t GetDpadTouchIncrement() const;
		void SetDpadTouchIncrement(uint16_t increment);

		void DeleteProfile(QString profile);

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

		ChiakiConnectVideoProfile GetVideoProfileLocalPS4();
		ChiakiConnectVideoProfile GetVideoProfileRemotePS4();
		ChiakiConnectVideoProfile GetVideoProfileLocalPS5();
		ChiakiConnectVideoProfile GetVideoProfileRemotePS5();

		DisconnectAction GetDisconnectAction();
		void SetDisconnectAction(DisconnectAction action);

		SuspendAction GetSuspendAction();
		void SetSuspendAction(SuspendAction action);

		PlaceboUpscaler GetPlaceboUpscaler() const;
		void SetPlaceboUpscaler(PlaceboUpscaler upscaler);

		PlaceboUpscaler GetPlaceboPlaneUpscaler() const;
		void SetPlaceboPlaneUpscaler(PlaceboUpscaler upscaler);
		
		PlaceboDownscaler GetPlaceboDownscaler() const;
		void SetPlaceboDownscaler(PlaceboDownscaler downscaler);

		PlaceboDownscaler GetPlaceboPlaneDownscaler() const;
		void SetPlaceboPlaneDownscaler(PlaceboDownscaler downscaler);

		PlaceboFrameMixer GetPlaceboFrameMixer() const;
		void SetPlaceboFrameMixer(PlaceboFrameMixer frame_mixer);

		float GetPlaceboAntiringingStrength() const;
		void SetPlaceboAntiringingStrength(float strength);

		bool GetPlaceboDebandEnabled() const;
		void SetPlaceboDebandEnabled(bool enabled);

		PlaceboDebandPreset GetPlaceboDebandPreset() const;
		void SetPlaceboDebandPreset(PlaceboDebandPreset preset);

		int GetPlaceboDebandIterations() const;
		void SetPlaceboDebandIterations(int iterations);

		float GetPlaceboDebandThreshold() const;
		void SetPlaceboDebandThreshold(float threshold);

		float GetPlaceboDebandRadius() const;
		void SetPlaceboDebandRadius(float radius);
		
		float GetPlaceboDebandGrain() const;
		void SetPlaceboDebandGrain(float grain);
		
		bool GetPlaceboSigmoidEnabled() const;
		void SetPlaceboSigmoidEnabled(bool enabled);

		PlaceboSigmoidPreset GetPlaceboSigmoidPreset() const;
		void SetPlaceboSigmoidPreset(PlaceboSigmoidPreset preset);
		
		float GetPlaceboSigmoidCenter() const;
		void SetPlaceboSigmoidCenter(float center);

		float GetPlaceboSigmoidSlope() const;
		void SetPlaceboSigmoidSlope(float slope);

		bool GetPlaceboColorAdjustmentEnabled() const;
		void SetPlaceboColorAdjustmentEnabled(bool enabled);

		PlaceboColorAdjustmentPreset GetPlaceboColorAdjustmentPreset() const;
		void SetPlaceboColorAdjustmentPreset(PlaceboColorAdjustmentPreset preset);

		float GetPlaceboColorAdjustmentBrightness() const;
		void SetPlaceboColorAdjustmentBrightness(float brightness);

		float GetPlaceboColorAdjustmentContrast() const;
		void SetPlaceboColorAdjustmentContrast(float contrast);

		float GetPlaceboColorAdjustmentSaturation() const;
		void SetPlaceboColorAdjustmentSaturation(float saturation);

		float GetPlaceboColorAdjustmentHue() const;
		void SetPlaceboColorAdjustmentHue(float hue);

		float GetPlaceboColorAdjustmentGamma() const;
		void SetPlaceboColorAdjustmentGamma(float gamma);

		float GetPlaceboColorAdjustmentTemperature() const;
		void SetPlaceboColorAdjustmentTemperature(float temperature);

		bool GetPlaceboPeakDetectionEnabled() const;
		void SetPlaceboPeakDetectionEnabled(bool enabled);

		PlaceboPeakDetectionPreset GetPlaceboPeakDetectionPreset() const;
		void SetPlaceboPeakDetectionPreset(PlaceboPeakDetectionPreset preset);

		float GetPlaceboPeakDetectionPeakSmoothingPeriod() const;
		void SetPlaceboPeakDetectionPeakSmoothingPeriod(float period);

		float GetPlaceboPeakDetectionSceneThresholdLow() const;
		void SetPlaceboPeakDetectionSceneThresholdLow(float threshold_low);

		float GetPlaceboPeakDetectionSceneThresholdHigh() const;
		void SetPlaceboPeakDetectionSceneThresholdHigh(float threshold_high);

		float GetPlaceboPeakDetectionPeakPercentile() const;
		void SetPlaceboPeakDetectionPeakPercentile(float percentile);

		float GetPlaceboPeakDetectionBlackCutoff() const;
		void SetPlaceboPeakDetectionBlackCutoff(float cutoff);

		bool GetPlaceboPeakDetectionAllowDelayedPeak() const;
		void SetPlaceboPeakDetectionAllowDelayedPeak(bool allowed);

		bool GetPlaceboColorMappingEnabled() const;
		void SetPlaceboColorMappingEnabled(bool enabled);

		PlaceboColorMappingPreset GetPlaceboColorMappingPreset() const;
		void SetPlaceboColorMappingPreset(PlaceboColorMappingPreset preset);

		PlaceboGamutMappingFunction GetPlaceboGamutMappingFunction() const;
		void SetPlaceboGamutMappingFunction(PlaceboGamutMappingFunction function);

		float GetPlaceboGamutMappingPerceptualDeadzone() const;
		void SetPlaceboGamutMappingPerceptualDeadzone(float deadzone);

		float GetPlaceboGamutMappingPerceptualStrength() const;
		void SetPlaceboGamutMappingPerceptualStrength(float strength);

		float GetPlaceboGamutMappingColorimetricGamma() const;
		void SetPlaceboGamutMappingColorimetricGamma(float gamma);

		float GetPlaceboGamutMappingSoftClipKnee() const;
		void SetPlaceboGamutMappingSoftClipKnee(float knee);

		float GetPlaceboGamutMappingSoftClipDesat() const;
		void SetPlaceboGamutMappingSoftClipDesat(float strength);

		int GetPlaceboGamutMappingLut3dSizeI() const;
		void SetPlaceboGamutMappingLut3dSizeI(int size);

		int GetPlaceboGamutMappingLut3dSizeC() const;
		void SetPlaceboGamutMappingLut3dSizeC(int size);

		int GetPlaceboGamutMappingLut3dSizeH() const;
		void SetPlaceboGamutMappingLut3dSizeH(int size);

		bool GetPlaceboGamutMappingLut3dTricubicEnabled() const;
		void SetPlaceboGamutMappingLut3dTricubicEnabled(bool enabled);

		bool GetPlaceboGamutMappingGamutExpansionEnabled() const;
		void SetPlaceboGamutMappingGamutExpansionEnabled(bool enabled);

		PlaceboToneMappingFunction GetPlaceboToneMappingFunction() const;
		void SetPlaceboToneMappingFunction(PlaceboToneMappingFunction function);

		float GetPlaceboToneMappingKneeAdaptation() const;
		void SetPlaceboToneMappingKneeAdaptation(float knee);

		float GetPlaceboToneMappingKneeMinimum() const;
		void SetPlaceboToneMappingKneeMinimum(float knee);

		float GetPlaceboToneMappingKneeMaximum() const;
		void SetPlaceboToneMappingKneeMaximum(float knee);

		float GetPlaceboToneMappingKneeDefault() const;
		void SetPlaceboToneMappingKneeDefault(float knee);

		float GetPlaceboToneMappingKneeOffset() const;
		void SetPlaceboToneMappingKneeOffset(float knee);

		float GetPlaceboToneMappingSlopeTuning() const;
		void SetPlaceboToneMappingSlopeTuning(float slope);

		float GetPlaceboToneMappingSlopeOffset() const;
		void SetPlaceboToneMappingSlopeOffset(float offset);

		float GetPlaceboToneMappingSplineContrast() const;
		void SetPlaceboToneMappingSplineContrast(float contrast);

		float GetPlaceboToneMappingReinhardContrast() const;
		void SetPlaceboToneMappingReinhardContrast(float contrast);

		float GetPlaceboToneMappingLinearKnee() const;
		void SetPlaceboToneMappingLinearKnee(float knee);

		float GetPlaceboToneMappingExposure() const;
		void SetPlaceboToneMappingExposure(float exposure);

		bool GetPlaceboToneMappingInverseToneMappingEnabled() const;
		void SetPlaceboToneMappingInverseToneMappingEnabled(bool enabled);

		PlaceboToneMappingMetadata GetPlaceboToneMappingMetadata() const;
		void SetPlaceboToneMappingMetadata(PlaceboToneMappingMetadata function);

		int GetPlaceboToneMappingToneLutSize() const;
		void SetPlaceboToneMappingToneLutSize(int size);

		float GetPlaceboToneMappingContrastRecovery() const;
		void SetPlaceboToneMappingContrastRecovery(float recovery);

		float GetPlaceboToneMappingContrastSmoothness() const;
		void SetPlaceboToneMappingContrastSmoothness(float smoothness);

		void PlaceboSettingsApply();

		QList<RegisteredHost> GetRegisteredHosts() const			{ return registered_hosts.values(); }
		void AddRegisteredHost(const RegisteredHost &host);
		void RemoveRegisteredHost(const HostMAC &mac);
		bool GetRegisteredHostRegistered(const HostMAC &mac) const	{ return registered_hosts.contains(mac); }
		RegisteredHost GetRegisteredHost(const HostMAC &mac) const	{ return registered_hosts[mac]; }
		QList<HiddenHost> GetHiddenHosts() const 					{ return hidden_hosts.values(); }
		void AddHiddenHost(const HiddenHost &host);
		void RemoveHiddenHost(const HostMAC &mac);
		bool GetHiddenHostHidden(const HostMAC &mac) const			{ return hidden_hosts.contains(mac); }
		HiddenHost GetHiddenHost(const HostMAC &mac) const 			{ return hidden_hosts[mac]; }
		bool GetNicknameRegisteredHostRegistered(const QString &nickname) const { return nickname_registered_hosts.contains(nickname); }
		RegisteredHost GetNicknameRegisteredHost(const QString &nickname) const { return nickname_registered_hosts[nickname]; }
		size_t GetPS4RegisteredHostsRegistered() const { return ps4s_registered; }
		QList<QString> GetProfiles() const                          { return profiles; }

		QList<ManualHost> GetManualHosts() const 					{ return manual_hosts.values(); }
		int SetManualHost(const ManualHost &host);
		void RemoveManualHost(int id);
		bool GetManualHostExists(int id)							{ return manual_hosts.contains(id); }
		ManualHost GetManualHost(int id) const						{ return manual_hosts[id]; }

		QMap<QString, QString> GetControllerMappings() const		{ return controller_mappings; }
		void SetControllerMapping(const QString &vidpid, const QString &mapping);
		void RemoveControllerMapping(const QString &vidpid);

		static QString GetChiakiControllerButtonName(int);
		void SetControllerButtonMapping(int, Qt::Key);
		QMap<int, Qt::Key> GetControllerMapping();
		QMap<Qt::Key, int> GetControllerMappingForDecoding();

	signals:
		void RegisteredHostsUpdated();
		void HiddenHostsUpdated();
		void ManualHostsUpdated();
		void ControllerMappingsUpdated();
		void CurrentProfileChanged();
		void ProfilesUpdated();
		void PlaceboSettingsUpdated();
};

#endif // CHIAKI_SETTINGS_H
