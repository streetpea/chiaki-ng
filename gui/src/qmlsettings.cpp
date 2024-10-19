#include "qmlsettings.h"
#include "sessionlog.h"

#include <QSet>
#include <QKeySequence>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QLoggingCategory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <SDL.h>
}

QmlSettings::QmlSettings(Settings *settings, QObject *parent)
    : QObject(parent)
    , settings(settings)
{
    if (settings->GetLogVerbose())
        QLoggingCategory::setFilterRules(QStringLiteral("chiaki.gui.debug=true"));

    connect(settings, &Settings::RegisteredHostsUpdated, this, &QmlSettings::registeredHostsChanged);
    connect(settings, &Settings::ProfilesUpdated, this, &QmlSettings::profilesChanged);
}

bool QmlSettings::remotePlayAsk() const
{
    return settings->GetRemotePlayAsk();
}

void QmlSettings::setRemotePlayAsk(bool asked)
{
    settings->SetRemotePlayAsk(asked);
    emit remotePlayAskChanged();
}

int QmlSettings::resolutionLocalPS4() const
{
    return settings->GetResolutionLocalPS4();
}

void QmlSettings::setResolutionLocalPS4(int resolution)
{
    settings->SetResolutionLocalPS4(static_cast<ChiakiVideoResolutionPreset>(resolution));
    emit resolutionLocalPS4Changed();
}

int QmlSettings::resolutionRemotePS4() const
{
    return settings->GetResolutionRemotePS4();
}

void QmlSettings::setResolutionRemotePS4(int resolution)
{
    settings->SetResolutionRemotePS4(static_cast<ChiakiVideoResolutionPreset>(resolution));
    emit resolutionRemotePS4Changed();
}

int QmlSettings::resolutionLocalPS5() const
{
    return settings->GetResolutionLocalPS5();
}

void QmlSettings::setResolutionLocalPS5(int resolution)
{
    settings->SetResolutionLocalPS5(static_cast<ChiakiVideoResolutionPreset>(resolution));
    emit resolutionLocalPS5Changed();
}

int QmlSettings::resolutionRemotePS5() const
{
    return settings->GetResolutionRemotePS5();
}

void QmlSettings::setResolutionRemotePS5(int resolution)
{
    settings->SetResolutionRemotePS5(static_cast<ChiakiVideoResolutionPreset>(resolution));
    emit resolutionRemotePS5Changed();
}

int QmlSettings::disconnectAction() const
{
    return static_cast<int>(settings->GetDisconnectAction());
}

void QmlSettings::setDisconnectAction(int action)
{
    settings->SetDisconnectAction(static_cast<DisconnectAction>(action));
    emit disconnectActionChanged();
}

int QmlSettings::suspendAction() const
{
    return static_cast<int>(settings->GetSuspendAction());
}

void QmlSettings::setSuspendAction(int action)
{
    settings->SetSuspendAction(static_cast<SuspendAction>(action));
    emit suspendActionChanged();
}

bool QmlSettings::logVerbose() const
{
    return settings->GetLogVerbose();
}

void QmlSettings::setLogVerbose(bool verbose)
{
    settings->SetLogVerbose(verbose);
    QLoggingCategory::setFilterRules(QStringLiteral("chiaki.gui.debug=%1").arg(verbose ? "true" : "false"));
    emit logVerboseChanged();
}

int QmlSettings::rumbleHapticsIntensity() const
{
    return static_cast<int>(settings->GetRumbleHapticsIntensity());
}

void QmlSettings::setRumbleHapticsIntensity(int intensity)
{
    settings->SetRumbleHapticsIntensity(static_cast<RumbleHapticsIntensity>(intensity));
    emit rumbleHapticsIntensityChanged();
}

#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
bool QmlSettings::steamDeckHaptics() const
{
    return settings->GetSteamDeckHapticsEnabled();
}

void QmlSettings::setSteamDeckHaptics(bool steamDeckHaptics)
{
    settings->SetSteamDeckHapticsEnabled(steamDeckHaptics);
    emit steamDeckHapticsChanged();
}

bool QmlSettings::verticalDeck() const
{
    return settings->GetVerticalDeckEnabled();
}

void QmlSettings::setVerticalDeck(bool vertical)
{
    settings->SetVerticalDeckEnabled(vertical);
    emit verticalDeckChanged();
}
#endif

bool QmlSettings::fullscreenDoubleClick() const
{
    return settings->GetFullscreenDoubleClickEnabled();
}

void QmlSettings::setFullscreenDoubleClick(bool enabled)
{
    settings->SetFullscreenDoubleClickEnabled(enabled);
    emit fullscreenDoubleClickChanged();
}

bool QmlSettings::buttonsByPosition() const
{
    return settings->GetButtonsByPosition();
}

void QmlSettings::setButtonsByPosition(bool buttonsByPosition)
{
    settings->SetButtonsByPosition(buttonsByPosition);
    emit buttonsByPositionChanged();
}

bool QmlSettings::startMicUnmuted() const
{
    return settings->GetStartMicUnmuted();
}

void QmlSettings::setStartMicUnmuted(bool startMicUnmuted)
{
    settings->SetStartMicUnmuted(startMicUnmuted);
    emit startMicUnmutedChanged();
}

#if CHIAKI_GUI_ENABLE_SPEEX
bool QmlSettings::speechProcessing() const
{
    return settings->GetSpeechProcessingEnabled();
}

void QmlSettings::setSpeechProcessing(bool processing)
{
    settings->SetSpeechProcessingEnabled(processing);
    emit speechProcessingChanged();
}

int QmlSettings::noiseSuppressLevel() const
{
    return settings->GetNoiseSuppressLevel();
}

void QmlSettings::setNoiseSuppressLevel(int level)
{
    settings->SetNoiseSuppressLevel(level);
    emit noiseSuppressLevelChanged();
}

int QmlSettings::echoSuppressLevel() const
{
    return settings->GetEchoSuppressLevel();
}

void QmlSettings::setEchoSuppressLevel(int level)
{
    settings->SetEchoSuppressLevel(level);
    emit echoSuppressLevelChanged();
}
#endif

int QmlSettings::fpsLocalPS4() const
{
    return settings->GetFPSLocalPS4();
}

void QmlSettings::setFpsLocalPS4(int fps)
{
    settings->SetFPSLocalPS4(static_cast<ChiakiVideoFPSPreset>(fps));
    emit fpsLocalPS4Changed();
}

int QmlSettings::fpsRemotePS4() const
{
    return settings->GetFPSRemotePS4();
}

void QmlSettings::setFpsRemotePS4(int fps)
{
    settings->SetFPSRemotePS4(static_cast<ChiakiVideoFPSPreset>(fps));
    emit fpsRemotePS4Changed();
}

int QmlSettings::fpsLocalPS5() const
{
    return settings->GetFPSLocalPS5();
}

void QmlSettings::setFpsLocalPS5(int fps)
{
    settings->SetFPSLocalPS5(static_cast<ChiakiVideoFPSPreset>(fps));
    emit fpsLocalPS5Changed();
}

int QmlSettings::fpsRemotePS5() const
{
    return settings->GetFPSRemotePS5();
}

void QmlSettings::setFpsRemotePS5(int fps)
{
    settings->SetFPSRemotePS5(static_cast<ChiakiVideoFPSPreset>(fps));
    emit fpsRemotePS5Changed();
}

int QmlSettings::bitrateLocalPS4() const
{
    return settings->GetBitrateLocalPS4();
}

void QmlSettings::setBitrateLocalPS4(int bitrate)
{
    settings->SetBitrateLocalPS4(bitrate);
    emit bitrateLocalPS4Changed();
}

int QmlSettings::bitrateRemotePS4() const
{
    return settings->GetBitrateRemotePS4();
}

void QmlSettings::setBitrateRemotePS4(int bitrate)
{
    settings->SetBitrateRemotePS4(bitrate);
    emit bitrateRemotePS4Changed();
}

int QmlSettings::bitrateLocalPS5() const
{
    return settings->GetBitrateLocalPS5();
}

void QmlSettings::setBitrateLocalPS5(int bitrate)
{
    settings->SetBitrateLocalPS5(bitrate);
    emit bitrateLocalPS5Changed();
}

int QmlSettings::bitrateRemotePS5() const
{
    return settings->GetBitrateRemotePS5();
}

void QmlSettings::setBitrateRemotePS5(int bitrate)
{
    settings->SetBitrateRemotePS5(bitrate);
    emit bitrateRemotePS5Changed();
}

int QmlSettings::codecLocalPS5() const
{
    return settings->GetCodecLocalPS5();
}

void QmlSettings::setCodecLocalPS5(int codec)
{
    settings->SetCodecLocalPS5(static_cast<ChiakiCodec>(codec));
    emit codecLocalPS5Changed();
}

int QmlSettings::codecRemotePS5() const
{
    return settings->GetCodecRemotePS5();
}

void QmlSettings::setCodecRemotePS5(int codec)
{
    settings->SetCodecRemotePS5(static_cast<ChiakiCodec>(codec));
    emit codecRemotePS5Changed();
}

int QmlSettings::audioBufferSize() const
{
    return settings->GetAudioBufferSizeRaw();
}

void QmlSettings::setAudioBufferSize(int size)
{
    settings->SetAudioBufferSize(size);
    emit audioBufferSizeChanged();
}

QString QmlSettings::audioInDevice() const
{
    return settings->GetAudioInDevice();
}

void QmlSettings::setAudioInDevice(const QString &device)
{
    settings->SetAudioInDevice(device);
    emit audioInDeviceChanged();
}

QString QmlSettings::audioOutDevice() const
{
    return settings->GetAudioOutDevice();
}

void QmlSettings::setAudioOutDevice(const QString &device)
{
    settings->SetAudioOutDevice(device);
    emit audioOutDeviceChanged();
}

QString QmlSettings::decoder() const
{
    return settings->GetHardwareDecoder();
}

void QmlSettings::setDecoder(const QString &decoder)
{
    settings->SetHardwareDecoder(decoder);
    emit decoderChanged();
}

int QmlSettings::windowType() const
{
    return static_cast<int>(settings->GetWindowType());
}

void QmlSettings::setWindowType(int type)
{
    settings->SetWindowType(static_cast<WindowType>(type));
    emit windowTypeChanged();
}

float QmlSettings::sZoomFactor() const
{
    return settings->GetZoomFactor();
}

void QmlSettings::setSZoomFactor(float factor)
{
    settings->SetZoomFactor(factor);
    emit sZoomFactorChanged();
}

int QmlSettings::packetLossMax() const
{
    return (settings->GetPacketLossMax() * 100);
}

void QmlSettings::setPacketLossMax(int packet_loss_max)
{
    float packet_loss = (float)packet_loss_max / (float)100;
    settings->SetPacketLossMax(packet_loss);
    emit packetLossMaxChanged();
}

int QmlSettings::videoPreset() const
{
    return static_cast<int>(settings->GetPlaceboPreset());
}

void QmlSettings::setVideoPreset(int preset)
{
    settings->SetPlaceboPreset(static_cast<PlaceboPreset>(preset));
    emit videoPresetChanged();
}

QString QmlSettings::autoConnectMac() const
{
    return settings->GetAutoConnectHost().GetServerMAC().ToString();
}

void QmlSettings::setAutoConnectMac(const QString &mac)
{
    settings->SetAutoConnectHost(QByteArray::fromHex(mac.toUtf8()));
    emit autoConnectMacChanged();
}

uint QmlSettings::wifiDroppedNotif() const
{
    return settings->GetWifiDroppedNotif();
}

void QmlSettings::setWifiDroppedNotif(const uint percent)
{
    settings->SetWifiDroppedNotif(percent);
    emit wifiDroppedNotifChanged();
}

QString QmlSettings::psnAuthToken() const
{
    return settings->GetPsnAuthToken();
}

void QmlSettings::setPsnAuthToken(const QString &auth_token)
{
    settings->SetPsnAuthToken(auth_token);
    emit psnAuthTokenChanged();
}

QString QmlSettings::psnRefreshToken() const
{
    return settings->GetPsnRefreshToken();
}

void QmlSettings::setPsnRefreshToken(const QString &refresh_token)
{
    settings->SetPsnRefreshToken(refresh_token);
    emit psnRefreshTokenChanged();
}

QString QmlSettings::psnAuthTokenExpiry() const
{
    return settings->GetPsnAuthTokenExpiry();
}

void QmlSettings::setPsnAuthTokenExpiry(const QString &expiry)
{
    settings->SetPsnAuthTokenExpiry(expiry);
    emit psnAuthTokenExpiryChanged();
}

QString QmlSettings::psnAccountId() const
{
    return settings->GetPsnAccountId();
}

void QmlSettings::setPsnAccountId(const QString &account_id)
{
    settings->SetPsnAccountId(account_id);
    emit psnAccountIdChanged();
}

bool QmlSettings::dpadTouchEnabled() const
{
    return settings->GetDpadTouchEnabled();
}

void QmlSettings::setDpadTouchEnabled(bool enabled)
{
    settings->SetDpadTouchEnabled(enabled);
    emit dpadTouchEnabledChanged();
}

uint16_t QmlSettings::dpadTouchIncrement() const
{
    return settings->GetDpadTouchIncrement();
}

void QmlSettings::setDpadTouchIncrement(uint16_t increment)
{
    settings->SetDpadTouchIncrement(increment);
    emit dpadTouchIncrementChanged();
}

QString QmlSettings::currentProfile() const
{
    return settings->GetCurrentProfile();
}

void QmlSettings::setCurrentProfile(const QString &profile)
{
    settings->SetCurrentProfile(profile);
    emit currentProfileChanged();
}

int QmlSettings::placeboUpscaler() const
{
    return static_cast<int>(settings->GetPlaceboUpscaler());
}
void QmlSettings::setPlaceboUpscaler(int upscaler)
{
    settings->SetPlaceboUpscaler(static_cast<PlaceboUpscaler>(upscaler));
    emit placeboUpscalerChanged();
}

int QmlSettings::placeboPlaneUpscaler() const
{
    return static_cast<int>(settings->GetPlaceboPlaneUpscaler());
}
void QmlSettings::setPlaceboPlaneUpscaler(int upscaler)
{
    settings->SetPlaceboPlaneUpscaler(static_cast<PlaceboUpscaler>(upscaler));
    emit placeboPlaneUpscalerChanged();
}

int QmlSettings::placeboDownscaler() const
{
    return static_cast<int>(settings->GetPlaceboDownscaler());
}
void QmlSettings::setPlaceboDownscaler(int downscaler)
{
    settings->SetPlaceboDownscaler(static_cast<PlaceboDownscaler>(downscaler));
    emit placeboDownscalerChanged();
}

int QmlSettings::placeboPlaneDownscaler() const
{
    return static_cast<int>(settings->GetPlaceboPlaneDownscaler());
}
void QmlSettings::setPlaceboPlaneDownscaler(int downscaler)
{
    settings->SetPlaceboPlaneDownscaler(static_cast<PlaceboDownscaler>(downscaler));
    emit placeboPlaneDownscalerChanged();
}

int QmlSettings::placeboFrameMixer() const
{
    return static_cast<int>(settings->GetPlaceboFrameMixer());
}
void QmlSettings::setPlaceboFrameMixer(int mixer)
{
    settings->SetPlaceboFrameMixer(static_cast<PlaceboFrameMixer>(mixer));
    emit placeboFrameMixerChanged();
}

float QmlSettings::placeboAntiringingStrength() const
{
    return settings->GetPlaceboAntiringingStrength();
}
void QmlSettings::setPlaceboAntiringingStrength(float strength)
{
    settings->SetPlaceboAntiringingStrength(strength);
    emit placeboAntiringingStrengthChanged();
}

bool QmlSettings::placeboDebandEnabled() const
{
    return settings->GetPlaceboDebandEnabled();
}
void QmlSettings::setPlaceboDebandEnabled(bool enabled)
{
    settings->SetPlaceboDebandEnabled(enabled);
    emit placeboDebandEnabledChanged();
}

int QmlSettings::placeboDebandPreset() const
{
    return static_cast<int>(settings->GetPlaceboDebandPreset());
}
void QmlSettings::setPlaceboDebandPreset(int preset)
{
    settings->SetPlaceboDebandPreset(static_cast<PlaceboDebandPreset>(preset));
    emit placeboDebandPresetChanged();
}

int QmlSettings::placeboDebandIterations() const
{
    return settings->GetPlaceboDebandIterations();
}
void QmlSettings::setPlaceboDebandIterations(int iterations)
{
    settings->SetPlaceboDebandIterations(iterations);
    emit placeboDebandIterationsChanged();
}

float QmlSettings::placeboDebandThreshold() const
{
    return settings->GetPlaceboDebandThreshold();
}
void QmlSettings::setPlaceboDebandThreshold(float threshold)
{
    settings->SetPlaceboDebandThreshold(threshold);
    emit placeboDebandThresholdChanged();
}

float QmlSettings::placeboDebandRadius() const
{
    return settings->GetPlaceboDebandRadius();
}
void QmlSettings::setPlaceboDebandRadius(float radius)
{
    settings->SetPlaceboDebandRadius(radius);
    emit placeboDebandRadiusChanged();
}

float QmlSettings::placeboDebandGrain() const
{
    return settings->GetPlaceboDebandGrain();
}
void QmlSettings::setPlaceboDebandGrain(float grain)
{
    settings->SetPlaceboDebandGrain(grain);
    emit placeboDebandGrainChanged();
}

bool QmlSettings::placeboSigmoidEnabled() const
{
    return settings->GetPlaceboSigmoidEnabled();
}
void QmlSettings::setPlaceboSigmoidEnabled(bool enabled)
{
    settings->SetPlaceboSigmoidEnabled(enabled);
    emit placeboSigmoidEnabledChanged();
}

int QmlSettings::placeboSigmoidPreset() const
{
    return static_cast<int>(settings->GetPlaceboSigmoidPreset());
}
void QmlSettings::setPlaceboSigmoidPreset(int preset)
{
    settings->SetPlaceboSigmoidPreset(static_cast<PlaceboSigmoidPreset>(preset));
    emit placeboSigmoidPresetChanged();
}

float QmlSettings::placeboSigmoidCenter() const
{
    return settings->GetPlaceboSigmoidCenter();
}
void QmlSettings::setPlaceboSigmoidCenter(float center)
{
    settings->SetPlaceboSigmoidCenter(center);
    emit placeboSigmoidCenterChanged();
}

float QmlSettings::placeboSigmoidSlope() const
{
    return settings->GetPlaceboSigmoidSlope();
}
void QmlSettings::setPlaceboSigmoidSlope(float slope)
{
    settings->SetPlaceboSigmoidSlope(slope);
    emit placeboSigmoidSlopeChanged();
}

bool QmlSettings::placeboColorAdjustmentEnabled() const
{
    return settings->GetPlaceboColorAdjustmentEnabled();
}
void QmlSettings::setPlaceboColorAdjustmentEnabled(bool enabled)
{
    settings->SetPlaceboColorAdjustmentEnabled(enabled);
    emit placeboColorAdjustmentEnabledChanged();
}

int QmlSettings::placeboColorAdjustmentPreset() const
{
    return static_cast<int>(settings->GetPlaceboColorAdjustmentPreset());
}
void QmlSettings::setPlaceboColorAdjustmentPreset(int preset)
{
    settings->SetPlaceboColorAdjustmentPreset(static_cast<PlaceboColorAdjustmentPreset>(preset));
    emit placeboColorAdjustmentPresetChanged();
}

float QmlSettings::placeboColorAdjustmentBrightness() const
{
    return settings->GetPlaceboColorAdjustmentBrightness();
}
void QmlSettings::setPlaceboColorAdjustmentBrightness(float brightness)
{
    settings->SetPlaceboColorAdjustmentBrightness(brightness);
    emit placeboColorAdjustmentBrightnessChanged();
}

float QmlSettings::placeboColorAdjustmentContrast() const
{
    return settings->GetPlaceboColorAdjustmentContrast();
}
void QmlSettings::setPlaceboColorAdjustmentContrast(float contrast)
{
    settings->SetPlaceboColorAdjustmentContrast(contrast);
    emit placeboColorAdjustmentContrastChanged();
}

float QmlSettings::placeboColorAdjustmentSaturation() const
{
    return settings->GetPlaceboColorAdjustmentSaturation();
}
void QmlSettings::setPlaceboColorAdjustmentSaturation(float saturation)
{
    settings->SetPlaceboColorAdjustmentSaturation(saturation);
    emit placeboColorAdjustmentSaturationChanged();
}

float QmlSettings::placeboColorAdjustmentHue() const
{
    return settings->GetPlaceboColorAdjustmentHue();
}
void QmlSettings::setPlaceboColorAdjustmentHue(float hue)
{
    settings->SetPlaceboColorAdjustmentHue(hue);
    emit placeboColorAdjustmentHueChanged();
}

float QmlSettings::placeboColorAdjustmentGamma() const
{
    return settings->GetPlaceboColorAdjustmentGamma();
}
void QmlSettings::setPlaceboColorAdjustmentGamma(float gamma)
{
    settings->SetPlaceboColorAdjustmentGamma(gamma);
    emit placeboColorAdjustmentGammaChanged();
}

float QmlSettings::placeboColorAdjustmentTemperature() const
{
    return settings->GetPlaceboColorAdjustmentTemperature();
}
void QmlSettings::setPlaceboColorAdjustmentTemperature(float temperature)
{
    settings->SetPlaceboColorAdjustmentTemperature(temperature);
    emit placeboColorAdjustmentTemperatureChanged();
}

bool QmlSettings::placeboPeakDetectionEnabled() const
{
    return settings->GetPlaceboPeakDetectionEnabled();
}
void QmlSettings::setPlaceboPeakDetectionEnabled(bool enabled)
{
    settings->SetPlaceboPeakDetectionEnabled(enabled);
    emit placeboPeakDetectionEnabledChanged();
}

int QmlSettings::placeboPeakDetectionPreset() const
{
    return static_cast<int>(settings->GetPlaceboPeakDetectionPreset());
}
void QmlSettings::setPlaceboPeakDetectionPreset(int preset)
{
    settings->SetPlaceboPeakDetectionPreset(static_cast<PlaceboPeakDetectionPreset>(preset));
    emit placeboPeakDetectionPresetChanged();
}

float QmlSettings::placeboPeakDetectionPeakSmoothingPeriod() const
{
    return settings->GetPlaceboPeakDetectionPeakSmoothingPeriod();
}
void QmlSettings::setPlaceboPeakDetectionPeakSmoothingPeriod(float period)
{
    settings->SetPlaceboPeakDetectionPeakSmoothingPeriod(period);
    emit placeboPeakDetectionPeakSmoothingPeriodChanged();
}

float QmlSettings::placeboPeakDetectionSceneThresholdLow() const
{
    return settings->GetPlaceboPeakDetectionSceneThresholdLow();
}
void QmlSettings::setPlaceboPeakDetectionSceneThresholdLow(float threshold_low)
{
    settings->SetPlaceboPeakDetectionSceneThresholdLow(threshold_low);
    emit placeboPeakDetectionSceneThresholdLowChanged();
}

float QmlSettings::placeboPeakDetectionSceneThresholdHigh() const
{
    return settings->GetPlaceboPeakDetectionSceneThresholdHigh();
}
void QmlSettings::setPlaceboPeakDetectionSceneThresholdHigh(float threshold_high)
{
    settings->SetPlaceboPeakDetectionSceneThresholdHigh(threshold_high);
    emit placeboPeakDetectionSceneThresholdHighChanged();
}

float QmlSettings::placeboPeakDetectionPeakPercentile() const
{
    return settings->GetPlaceboPeakDetectionPeakPercentile();
}
void QmlSettings::setPlaceboPeakDetectionPeakPercentile(float percentile)
{
    settings->SetPlaceboPeakDetectionPeakPercentile(percentile);
    emit placeboPeakDetectionPeakPercentileChanged();
}

float QmlSettings::placeboPeakDetectionBlackCutoff() const
{
    return settings->GetPlaceboPeakDetectionBlackCutoff();
}
void QmlSettings::setPlaceboPeakDetectionBlackCutoff(float cutoff)
{
    settings->SetPlaceboPeakDetectionBlackCutoff(cutoff);
    emit placeboPeakDetectionBlackCutoffChanged();
}

bool QmlSettings::placeboPeakDetectionAllowDelayedPeak() const
{
    return settings->GetPlaceboPeakDetectionAllowDelayedPeak();
}
void QmlSettings::setPlaceboPeakDetectionAllowDelayedPeak(bool allowed)
{
    settings->SetPlaceboPeakDetectionAllowDelayedPeak(allowed);
    emit placeboPeakDetectionAllowDelayedPeakChanged();
}

bool QmlSettings::placeboColorMappingEnabled() const
{
    return settings->GetPlaceboColorMappingEnabled();
}
void QmlSettings::setPlaceboColorMappingEnabled(bool enabled)
{
    settings->SetPlaceboColorMappingEnabled(enabled);
    emit placeboColorMappingEnabledChanged();
}

int QmlSettings::placeboColorMappingPreset() const
{
    return static_cast<int>(settings->GetPlaceboColorMappingPreset());
}
void QmlSettings::setPlaceboColorMappingPreset(int preset)
{
    settings->SetPlaceboColorMappingPreset(static_cast<PlaceboColorMappingPreset>(preset));
    emit placeboColorMappingPresetChanged();
}

int QmlSettings::placeboGamutMappingFunction() const
{
    return static_cast<int>(settings->GetPlaceboGamutMappingFunction());
}
void QmlSettings::setPlaceboGamutMappingFunction(int function)
{
    settings->SetPlaceboGamutMappingFunction(static_cast<PlaceboGamutMappingFunction>(function));
    emit placeboGamutMappingFunctionChanged();
}

float QmlSettings::placeboGamutMappingPerceptualDeadzone() const
{
    return settings->GetPlaceboGamutMappingPerceptualDeadzone();
}
void QmlSettings::setPlaceboGamutMappingPerceptualDeadzone(float deadzone)
{
    settings->SetPlaceboGamutMappingPerceptualDeadzone(deadzone);
    emit placeboGamutMappingPerceptualDeadzoneChanged();
}

float QmlSettings::placeboGamutMappingPerceptualStrength() const
{
    return settings->GetPlaceboGamutMappingPerceptualStrength();
}
void QmlSettings::setPlaceboGamutMappingPerceptualStrength(float strength)
{
    settings->SetPlaceboGamutMappingPerceptualStrength(strength);
    emit placeboGamutMappingPerceptualStrengthChanged();
}

float QmlSettings::placeboGamutMappingColorimetricGamma() const
{
    return settings->GetPlaceboGamutMappingColorimetricGamma();
}
void QmlSettings::setPlaceboGamutMappingColorimetricGamma(float gamma)
{
    settings->SetPlaceboGamutMappingColorimetricGamma(gamma);
    emit placeboGamutMappingColorimetricGammaChanged();
}

float QmlSettings::placeboGamutMappingSoftClipKnee() const
{
    return settings->GetPlaceboGamutMappingSoftClipKnee();
}
void QmlSettings::setPlaceboGamutMappingSoftClipKnee(float knee)
{
    settings->SetPlaceboGamutMappingSoftClipKnee(knee);
    emit placeboGamutMappingSoftClipKneeChanged();
}

float QmlSettings::placeboGamutMappingSoftClipDesat() const
{
    return settings->GetPlaceboGamutMappingSoftClipDesat();
}
void QmlSettings::setPlaceboGamutMappingSoftClipDesat(float desat)
{
    settings->SetPlaceboGamutMappingSoftClipDesat(desat);
    emit placeboGamutMappingSoftClipDesatChanged();
}

int QmlSettings::placeboGamutMappingLut3dSizeI() const
{
    return settings->GetPlaceboGamutMappingLut3dSizeI();
}
void QmlSettings::setPlaceboGamutMappingLut3dSizeI(int size)
{
    settings->SetPlaceboGamutMappingLut3dSizeI(size);
    emit placeboGamutMappingLut3dSizeIChanged();
}

int QmlSettings::placeboGamutMappingLut3dSizeC() const
{
    return settings->GetPlaceboGamutMappingLut3dSizeC();
}
void QmlSettings::setPlaceboGamutMappingLut3dSizeC(int size)
{
    settings->SetPlaceboGamutMappingLut3dSizeC(size);
    emit placeboGamutMappingLut3dSizeCChanged();
}

int QmlSettings::placeboGamutMappingLut3dSizeH() const
{
    return settings->GetPlaceboGamutMappingLut3dSizeH();
}
void QmlSettings::setPlaceboGamutMappingLut3dSizeH(int size)
{
    settings->SetPlaceboGamutMappingLut3dSizeH(size);
    emit placeboGamutMappingLut3dSizeHChanged();
}

bool QmlSettings::placeboGamutMappingLut3dTricubicEnabled() const
{
    return settings->GetPlaceboGamutMappingLut3dTricubicEnabled();
}
void QmlSettings::setPlaceboGamutMappingLut3dTricubicEnabled(bool enabled)
{
    settings->SetPlaceboGamutMappingLut3dTricubicEnabled(enabled);
    emit placeboGamutMappingLut3dTricubicEnabledChanged();
}

bool QmlSettings::placeboGamutMappingGamutExpansionEnabled() const
{
    return settings->GetPlaceboGamutMappingGamutExpansionEnabled();
}
void QmlSettings::setPlaceboGamutMappingGamutExpansionEnabled(bool enabled)
{
    settings->SetPlaceboGamutMappingGamutExpansionEnabled(enabled);
    emit placeboGamutMappingGamutExpansionEnabledChanged();
}

int QmlSettings::placeboToneMappingFunction() const
{
    return static_cast<int>(settings->GetPlaceboToneMappingFunction());
}
void QmlSettings::setPlaceboToneMappingFunction(int function)
{
    settings->SetPlaceboToneMappingFunction(static_cast<PlaceboToneMappingFunction>(function));
    emit placeboToneMappingFunctionChanged();
}

float QmlSettings::placeboToneMappingKneeAdaptation() const
{
    return settings->GetPlaceboToneMappingKneeAdaptation();
}
void QmlSettings::setPlaceboToneMappingKneeAdaptation(float adaptation)
{
    settings->SetPlaceboToneMappingKneeAdaptation(adaptation);
    emit placeboToneMappingKneeAdaptationChanged();
}

float QmlSettings::placeboToneMappingKneeMinimum() const
{
    return settings->GetPlaceboToneMappingKneeMinimum();
}
void QmlSettings::setPlaceboToneMappingKneeMinimum(float min)
{
    settings->SetPlaceboToneMappingKneeMinimum(min);
    emit placeboToneMappingKneeMinimumChanged();
}

float QmlSettings::placeboToneMappingKneeMaximum() const
{
    return settings->GetPlaceboToneMappingKneeMaximum();
}
void QmlSettings::setPlaceboToneMappingKneeMaximum(float max)
{
    settings->SetPlaceboToneMappingKneeMaximum(max);
    emit placeboToneMappingKneeMaximumChanged();
}

float QmlSettings::placeboToneMappingKneeDefault() const
{
    return settings->GetPlaceboToneMappingKneeDefault();
}
void QmlSettings::setPlaceboToneMappingKneeDefault(float default_knee)
{
    settings->SetPlaceboToneMappingKneeDefault(default_knee);
    emit placeboToneMappingKneeDefaultChanged();
}

float QmlSettings::placeboToneMappingKneeOffset() const
{
    return settings->GetPlaceboToneMappingKneeOffset();
}
void QmlSettings::setPlaceboToneMappingKneeOffset(float offset)
{
    settings->SetPlaceboToneMappingKneeOffset(offset);
    emit placeboToneMappingKneeOffsetChanged();
}

float QmlSettings::placeboToneMappingSlopeTuning() const
{
    return settings->GetPlaceboToneMappingSlopeTuning();
}
void QmlSettings::setPlaceboToneMappingSlopeTuning(float tuning)
{
    settings->SetPlaceboToneMappingSlopeTuning(tuning);
    emit placeboToneMappingSlopeTuningChanged();
}

float QmlSettings::placeboToneMappingSlopeOffset() const
{
    return settings->GetPlaceboToneMappingSlopeOffset();
}
void QmlSettings::setPlaceboToneMappingSlopeOffset(float offset)
{
    settings->SetPlaceboToneMappingSlopeOffset(offset);
    emit placeboToneMappingSlopeOffsetChanged();
}

float QmlSettings::placeboToneMappingSplineContrast() const
{
    return settings->GetPlaceboToneMappingSplineContrast();
}
void QmlSettings::setPlaceboToneMappingSplineContrast(float contrast)
{
    settings->SetPlaceboToneMappingSplineContrast(contrast);
    emit placeboToneMappingSplineContrastChanged();
}

float QmlSettings::placeboToneMappingReinhardContrast() const
{
    return settings->GetPlaceboToneMappingReinhardContrast();
}
void QmlSettings::setPlaceboToneMappingReinhardContrast(float contrast)
{
    settings->SetPlaceboToneMappingReinhardContrast(contrast);
    emit placeboToneMappingReinhardContrastChanged();
}

float QmlSettings::placeboToneMappingLinearKnee() const
{
    return settings->GetPlaceboToneMappingLinearKnee();
}
void QmlSettings::setPlaceboToneMappingLinearKnee(float knee)
{
    settings->SetPlaceboToneMappingLinearKnee(knee);
    emit placeboToneMappingLinearKneeChanged();
}

float QmlSettings::placeboToneMappingExposure() const
{
    return settings->GetPlaceboToneMappingExposure();
}
void QmlSettings::setPlaceboToneMappingExposure(float exposure)
{
    settings->SetPlaceboToneMappingExposure(exposure);
    emit placeboToneMappingExposureChanged();
}

bool QmlSettings::placeboToneMappingInverseToneMappingEnabled() const
{
    return settings->GetPlaceboToneMappingInverseToneMappingEnabled();
}
void QmlSettings::setPlaceboToneMappingInverseToneMappingEnabled(bool enabled)
{
    settings->SetPlaceboToneMappingInverseToneMappingEnabled(enabled);
    emit placeboToneMappingInverseToneMappingEnabledChanged();
}

int QmlSettings::placeboToneMappingMetadata() const
{
    return static_cast<int>(settings->GetPlaceboToneMappingMetadata());
}
void QmlSettings::setPlaceboToneMappingMetadata(int metadata)
{
    settings->SetPlaceboToneMappingMetadata(static_cast<PlaceboToneMappingMetadata>(metadata));
    emit placeboToneMappingMetadataChanged();
}

int QmlSettings::placeboToneMappingToneLutSize() const
{
    return settings->GetPlaceboToneMappingToneLutSize();
}
void QmlSettings::setPlaceboToneMappingToneLutSize(int size)
{
    settings->SetPlaceboToneMappingToneLutSize(size);
    emit placeboToneMappingToneLutSizeChanged();
}

float QmlSettings::placeboToneMappingContrastRecovery() const
{
    return settings->GetPlaceboToneMappingContrastRecovery();
}
void QmlSettings::setPlaceboToneMappingContrastRecovery(float recovery)
{
    settings->SetPlaceboToneMappingContrastRecovery(recovery);
    emit placeboToneMappingContrastRecoveryChanged();
}

float QmlSettings::placeboToneMappingContrastSmoothness() const
{
    return settings->GetPlaceboToneMappingContrastSmoothness();
}
void QmlSettings::setPlaceboToneMappingContrastSmoothness(float smoothness)
{
    settings->SetPlaceboToneMappingContrastSmoothness(smoothness);
    emit placeboToneMappingContrastSmoothnessChanged();
}

QString QmlSettings::logDirectory() const
{
    return GetLogBaseDir();
}

void QmlSettings::clearKeyMapping()
{
    settings->ClearKeyMapping();
    emit controllerMappingChanged();
}

QStringList QmlSettings::availableDecoders() const
{
    static QSet<QString> allowed = {
        "vulkan",
#if defined(Q_OS_LINUX)
        "vaapi",
#elif defined(Q_OS_MACOS)
        "videotoolbox",
#elif defined(Q_OS_WIN)
        "d3d11va",
#endif
    };
    QStringList out = {"none"};
    enum AVHWDeviceType hw_dev = AV_HWDEVICE_TYPE_NONE;
    while (true) {
        hw_dev = av_hwdevice_iterate_types(hw_dev);
        if (hw_dev == AV_HWDEVICE_TYPE_NONE)
            break;
        const QString name = QString::fromUtf8(av_hwdevice_get_type_name(hw_dev));
        if (allowed.contains(name))
            out.append(name);
    }
    out.append("auto");
    return out;
}

QStringList QmlSettings::availableAudioOutDevices() const
{
    return audio_out_devices;
}

QStringList QmlSettings::availableAudioInDevices() const
{
    return audio_in_devices;
}

QVariantList QmlSettings::registeredHosts() const
{
    QVariantList out;
    for (const auto &host : settings->GetRegisteredHosts()) {
        QVariantMap m;
        m["name"] = host.GetServerNickname();
        m["mac"] = host.GetServerMAC().ToString();
        m["ps5"] = chiaki_target_is_ps5(host.GetTarget());
        out.append(m);
    }
    return out;
}

QStringList QmlSettings::profiles() const
{
    QStringList out = settings->GetProfiles();
    out.prepend("default");
    out.append("create new profile");
    return out;
}

void QmlSettings::deleteProfile(QString profile)
{
    if(!profile.isEmpty())
        settings->DeleteProfile(std::move(profile));
}

QVariantList QmlSettings::controllerMapping() const
{
    QVariantList out;
    auto key_map = settings->GetControllerMapping();
    for (auto it = key_map.cbegin(); it != key_map.cend(); ++it) {
        QVariantMap m;
        m["buttonName"] = Settings::GetChiakiControllerButtonName(it.key());
        m["buttonValue"] = it.key();
        m["keyName"] = QKeySequence(it.value()).toString();
        out.append(m);
    }
    return out;
}

void QmlSettings::deleteRegisteredHost(int index)
{
    settings->RemoveRegisteredHost(settings->GetRegisteredHosts().value(index).GetServerMAC());
}

void QmlSettings::refreshAudioDevices()
{
    using Result = QPair<QStringList,QStringList>;
    auto watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        const Result result = watcher->result();
        audio_in_devices = result.first;
        audio_out_devices = result.second;
        emit audioDevicesChanged();
    });
    watcher->setFuture(QtConcurrent::run([]() {
        Result res;
        int count = SDL_GetNumAudioDevices(true);
        for (int i = 0; i < count; i++)
            res.first.append(SDL_GetAudioDeviceName(i, true));
        count = SDL_GetNumAudioDevices(false);
        for (int i = 0; i < count; i++)
            res.second.append(SDL_GetAudioDeviceName(i, false));
        return res;
    }));
}

QString QmlSettings::changeControllerKey(int button, int key)
{
    Qt::Key qt_key = Qt::Key(key);
    settings->SetControllerButtonMapping(button, qt_key);
    return QKeySequence(qt_key).toString();
}

void QmlSettings::setSettings(Settings *new_settings)
{
    settings = new_settings;
    connect(settings, &Settings::RegisteredHostsUpdated, this, &QmlSettings::registeredHostsChanged);
    connect(settings, &Settings::ProfilesUpdated, this, &QmlSettings::profilesChanged);
    refreshAllKeys();
}

void QmlSettings::refreshAllKeys()
{
    emit remotePlayAskChanged();
    emit resolutionLocalPS4Changed();
    emit resolutionRemotePS4Changed();
    emit resolutionLocalPS5Changed();
    emit resolutionRemotePS5Changed();
    emit disconnectActionChanged();
    emit suspendActionChanged();
    emit logVerboseChanged();
    emit rumbleHapticsIntensityChanged();
    emit buttonsByPositionChanged();
    emit startMicUnmutedChanged();
#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
    emit verticalDeckChanged();
    emit steamDeckHapticsChanged();
#endif
#ifdef CHIAKI_GUI_ENABLE_SPEEX
    emit speechProcessingChanged();
    emit noiseSuppressLevelChanged();
    emit echoSuppressLevelChanged();
#endif
    emit fullscreenDoubleClickChanged();
    emit fpsLocalPS4Changed();
    emit fpsRemotePS4Changed();
    emit fpsLocalPS5Changed();
    emit fpsRemotePS5Changed();
    emit bitrateLocalPS4Changed();
    emit bitrateRemotePS4Changed();
    emit bitrateLocalPS5Changed();
    emit bitrateRemotePS5Changed();
    emit codecLocalPS5Changed();
    emit codecRemotePS5Changed();
    emit audioBufferSizeChanged();
    emit audioOutDeviceChanged();
    emit audioInDeviceChanged();
    emit wifiDroppedNotifChanged();
    emit decoderChanged();
    emit windowTypeChanged();
    emit sZoomFactorChanged();
    emit videoPresetChanged();
    emit autoConnectMacChanged();
    emit audioDevicesChanged();
    emit registeredHostsChanged();
    emit psnAuthTokenChanged();
    emit psnRefreshTokenChanged();
    emit psnAuthTokenExpiryChanged();
    emit psnAccountIdChanged();
    emit dpadTouchIncrementChanged();
    emit controllerMappingChanged();
    emit packetLossMaxChanged();
    emit currentProfileChanged();
    emit profilesChanged();
    refreshAllPlaceboKeys();
}

void QmlSettings::refreshAllPlaceboKeys()
{
    emit placeboUpscalerChanged();
    emit placeboPlaneUpscalerChanged();
    emit placeboDownscalerChanged();
    emit placeboPlaneDownscalerChanged();
    emit placeboFrameMixerChanged();
    emit placeboAntiringingStrengthChanged();
    emit placeboDebandEnabledChanged();
    emit placeboDebandPresetChanged();
    emit placeboDebandIterationsChanged();
    emit placeboDebandThresholdChanged();
    emit placeboDebandRadiusChanged();
    emit placeboDebandGrainChanged();
    emit placeboSigmoidEnabledChanged();
    emit placeboSigmoidPresetChanged();
    emit placeboSigmoidCenterChanged();
    emit placeboSigmoidSlopeChanged();
    emit placeboColorAdjustmentEnabledChanged();
    emit placeboColorAdjustmentPresetChanged();
    emit placeboColorAdjustmentBrightnessChanged();
    emit placeboColorAdjustmentContrastChanged();
    emit placeboColorAdjustmentSaturationChanged();
    emit placeboColorAdjustmentHueChanged();
    emit placeboColorAdjustmentGammaChanged();
    emit placeboColorAdjustmentTemperatureChanged();
    emit placeboPeakDetectionEnabled();
    emit placeboPeakDetectionPresetChanged();
    emit placeboPeakDetectionPeakSmoothingPeriodChanged();
    emit placeboPeakDetectionSceneThresholdLowChanged();
    emit placeboPeakDetectionSceneThresholdHighChanged();
    emit placeboPeakDetectionPeakPercentileChanged();
    emit placeboPeakDetectionBlackCutoffChanged();
    emit placeboPeakDetectionAllowDelayedPeakChanged();
    emit placeboColorMappingEnabledChanged();
    emit placeboColorMappingPresetChanged();
    emit placeboGamutMappingFunctionChanged();
    emit placeboGamutMappingPerceptualDeadzoneChanged();
    emit placeboGamutMappingPerceptualStrengthChanged();
    emit placeboGamutMappingColorimetricGammaChanged();
    emit placeboGamutMappingSoftClipKneeChanged();
    emit placeboGamutMappingSoftClipDesatChanged();
    emit placeboGamutMappingLut3dSizeIChanged();
    emit placeboGamutMappingLut3dSizeCChanged();
    emit placeboGamutMappingLut3dSizeHChanged();
    emit placeboGamutMappingLut3dTricubicEnabledChanged();
    emit placeboGamutMappingGamutExpansionEnabledChanged();
    emit placeboToneMappingFunctionChanged();
    emit placeboToneMappingKneeAdaptationChanged();
    emit placeboToneMappingKneeMinimumChanged();
    emit placeboToneMappingKneeMaximumChanged();
    emit placeboToneMappingKneeDefaultChanged();
    emit placeboToneMappingKneeOffsetChanged();
    emit placeboToneMappingSlopeTuningChanged();
    emit placeboToneMappingSlopeOffsetChanged();
    emit placeboToneMappingSplineContrastChanged();
    emit placeboToneMappingReinhardContrastChanged();
    emit placeboToneMappingLinearKneeChanged();
    emit placeboToneMappingExposureChanged();
    emit placeboToneMappingInverseToneMappingEnabledChanged();
    emit placeboToneMappingMetadataChanged();
    emit placeboToneMappingToneLutSizeChanged();
    emit placeboToneMappingContrastRecoveryChanged();
}

void QmlSettings::exportSettings(QString fileurl)
{
    settings->ExportSettings(std::move(fileurl));
}

void QmlSettings::importSettings(QString fileurl)
{
    settings->ImportSettings(std::move(fileurl));
}

void QmlSettings::exportPlaceboSettings(QString fileurl)
{
    settings->ExportPlaceboSettings(std::move(fileurl));
}

void QmlSettings::importPlaceboSettings(QString fileurl)
{
    settings->ImportPlaceboSettings(std::move(fileurl));
    refreshAllPlaceboKeys();
}
