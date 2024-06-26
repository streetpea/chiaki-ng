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

bool QmlSettings::dualSense() const
{
    return settings->GetDualSenseEnabled();
}

void QmlSettings::setDualSense(bool dualSense)
{
    settings->SetDualSenseEnabled(dualSense);
    emit dualSenseChanged();
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

QString QmlSettings::currentProfile() const
{
    return settings->GetCurrentProfile();
}

void QmlSettings::setCurrentProfile(const QString &profile)
{
    settings->SetCurrentProfile(profile);
    emit currentProfileChanged();
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
        settings->DeleteProfile(profile);
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
    emit resolutionLocalPS4Changed();
    emit resolutionRemotePS4Changed();
    emit resolutionLocalPS5Changed();
    emit resolutionRemotePS5Changed();
    emit disconnectActionChanged();
    emit suspendActionChanged();
    emit logVerboseChanged();
    emit dualSenseChanged();
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
    emit controllerMappingChanged();
    emit packetLossMaxChanged();
    emit currentProfileChanged();
    emit profilesChanged();
}

void QmlSettings::exportSettings(QString fileurl)
{
    settings->ExportSettings(fileurl);
}

void QmlSettings::importSettings(QString fileurl)
{
    settings->ImportSettings(fileurl);
    refreshAllKeys();
}
