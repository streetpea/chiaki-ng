#pragma once

#include "settings.h"

class QmlSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int resolutionLocalPS4 READ resolutionLocalPS4 WRITE setResolutionLocalPS4 NOTIFY resolutionLocalPS4Changed)
    Q_PROPERTY(int resolutionRemotePS4 READ resolutionRemotePS4 WRITE setResolutionRemotePS4 NOTIFY resolutionRemotePS4Changed)
    Q_PROPERTY(int resolutionLocalPS5 READ resolutionLocalPS5 WRITE setResolutionLocalPS5 NOTIFY resolutionLocalPS5Changed)
    Q_PROPERTY(int resolutionRemotePS5 READ resolutionRemotePS5 WRITE setResolutionRemotePS5 NOTIFY resolutionRemotePS5Changed)
    Q_PROPERTY(int disconnectAction READ disconnectAction WRITE setDisconnectAction NOTIFY disconnectActionChanged)
    Q_PROPERTY(int suspendAction READ suspendAction WRITE setSuspendAction NOTIFY suspendActionChanged)
    Q_PROPERTY(bool logVerbose READ logVerbose WRITE setLogVerbose NOTIFY logVerboseChanged)
    Q_PROPERTY(bool dualSense READ dualSense WRITE setDualSense NOTIFY dualSenseChanged)
#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
    Q_PROPERTY(bool steamDeckHaptics READ steamDeckHaptics WRITE setSteamDeckHaptics NOTIFY steamDeckHapticsChanged)
    Q_PROPERTY(bool verticalDeck READ verticalDeck WRITE setVerticalDeck NOTIFY verticalDeckChanged)
#endif
    Q_PROPERTY(bool buttonsByPosition READ buttonsByPosition WRITE setButtonsByPosition NOTIFY buttonsByPositionChanged)
    Q_PROPERTY(bool startMicUnmuted READ startMicUnmuted WRITE setStartMicUnmuted NOTIFY startMicUnmutedChanged)
#ifdef CHIAKI_GUI_ENABLE_SPEEX
    Q_PROPERTY(bool speechProcessing READ speechProcessing WRITE setSpeechProcessing NOTIFY speechProcessingChanged)
    Q_PROPERTY(int noiseSuppressLevel READ noiseSuppressLevel WRITE setNoiseSuppressLevel NOTIFY noiseSuppressLevelChanged)
    Q_PROPERTY(int echoSuppressLevel READ echoSuppressLevel WRITE setEchoSuppressLevel NOTIFY echoSuppressLevelChanged)
#endif
    Q_PROPERTY(int fpsLocalPS4 READ fpsLocalPS4 WRITE setFpsLocalPS4 NOTIFY fpsLocalPS4Changed)
    Q_PROPERTY(int fpsRemotePS4 READ fpsRemotePS4 WRITE setFpsRemotePS4 NOTIFY fpsRemotePS4Changed)
    Q_PROPERTY(int fpsLocalPS5 READ fpsLocalPS5 WRITE setFpsLocalPS5 NOTIFY fpsLocalPS5Changed)
    Q_PROPERTY(int fpsRemotePS5 READ fpsRemotePS5 WRITE setFpsRemotePS5 NOTIFY fpsRemotePS5Changed)
    Q_PROPERTY(int bitrateLocalPS4 READ bitrateLocalPS4 WRITE setBitrateLocalPS4 NOTIFY bitrateLocalPS4Changed)
    Q_PROPERTY(int bitrateRemotePS4 READ bitrateRemotePS4 WRITE setBitrateRemotePS4 NOTIFY bitrateRemotePS4Changed)
    Q_PROPERTY(int bitrateLocalPS5 READ bitrateLocalPS5 WRITE setBitrateLocalPS5 NOTIFY bitrateLocalPS5Changed)
    Q_PROPERTY(int bitrateRemotePS5 READ bitrateRemotePS5 WRITE setBitrateRemotePS5 NOTIFY bitrateRemotePS5Changed)
    Q_PROPERTY(int codecLocalPS5 READ codecLocalPS5 WRITE setCodecLocalPS5 NOTIFY codecLocalPS5Changed)
    Q_PROPERTY(int codecRemotePS5 READ codecRemotePS5 WRITE setCodecRemotePS5 NOTIFY codecRemotePS5Changed)
    Q_PROPERTY(int audioBufferSize READ audioBufferSize WRITE setAudioBufferSize NOTIFY audioBufferSizeChanged)
    Q_PROPERTY(QString audioInDevice READ audioInDevice WRITE setAudioInDevice NOTIFY audioInDeviceChanged)
    Q_PROPERTY(QString audioOutDevice READ audioOutDevice WRITE setAudioOutDevice NOTIFY audioOutDeviceChanged)
    Q_PROPERTY(QString decoder READ decoder WRITE setDecoder NOTIFY decoderChanged)
    Q_PROPERTY(int windowType READ windowType WRITE setWindowType NOTIFY windowTypeChanged)
    Q_PROPERTY(int videoPreset READ videoPreset WRITE setVideoPreset NOTIFY videoPresetChanged)
    Q_PROPERTY(float sZoomFactor READ sZoomFactor WRITE setSZoomFactor NOTIFY sZoomFactorChanged)
    Q_PROPERTY(int packetLossMax READ packetLossMax WRITE setPacketLossMax NOTIFY packetLossMaxChanged)
    Q_PROPERTY(QString autoConnectMac READ autoConnectMac WRITE setAutoConnectMac NOTIFY autoConnectMacChanged)
    Q_PROPERTY(QString logDirectory READ logDirectory CONSTANT)
    Q_PROPERTY(QStringList availableDecoders READ availableDecoders CONSTANT)
    Q_PROPERTY(QStringList availableAudioInDevices READ availableAudioInDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(QStringList availableAudioOutDevices READ availableAudioOutDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(QVariantList registeredHosts READ registeredHosts NOTIFY registeredHostsChanged)
    Q_PROPERTY(QVariantList controllerMapping READ controllerMapping NOTIFY controllerMappingChanged)
    Q_PROPERTY(uint wifiDroppedNotif READ wifiDroppedNotif WRITE setWifiDroppedNotif NOTIFY wifiDroppedNotifChanged)
    Q_PROPERTY(QString psnRefreshToken READ psnRefreshToken WRITE setPsnRefreshToken NOTIFY psnRefreshTokenChanged)
    Q_PROPERTY(QString psnAuthToken READ psnAuthToken WRITE setPsnAuthToken NOTIFY psnAuthTokenChanged)
    Q_PROPERTY(QString psnAuthTokenExpiry READ psnAuthTokenExpiry WRITE setPsnAuthTokenExpiry NOTIFY psnAuthTokenExpiryChanged)
    Q_PROPERTY(QString psnAccountId READ psnAccountId WRITE setPsnAccountId NOTIFY psnAccountIdChanged)

public:
    QmlSettings(Settings *settings, QObject *parent = nullptr);

    int resolutionLocalPS4() const;
    void setResolutionLocalPS4(int resolution);
    int resolutionRemotePS4() const;
    void setResolutionRemotePS4(int resolution);
    int resolutionLocalPS5() const;
    void setResolutionLocalPS5(int resolution);
    int resolutionRemotePS5() const;
    void setResolutionRemotePS5(int resolution);

    int disconnectAction() const;
    void setDisconnectAction(int action);

    int suspendAction() const;
    void setSuspendAction(int action);

    bool logVerbose() const;
    void setLogVerbose(bool verbose);

    bool dualSense() const;
    void setDualSense(bool dualSense);

#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
    bool steamDeckHaptics() const;
    void setSteamDeckHaptics(bool steamDeckHaptics);

    bool verticalDeck() const;
    void setVerticalDeck(bool vertical);
#endif

    bool buttonsByPosition() const;
    void setButtonsByPosition(bool buttonsByPosition);

    bool startMicUnmuted() const;
    void setStartMicUnmuted(bool startMicUnmuted);

#ifdef CHIAKI_GUI_ENABLE_SPEEX
    bool speechProcessing() const;
    void setSpeechProcessing(bool processing);

    int noiseSuppressLevel() const;
    void setNoiseSuppressLevel(int level);

    int echoSuppressLevel() const;
    void setEchoSuppressLevel(int level);
#endif

    int fpsLocalPS4() const;
    void setFpsLocalPS4(int fps);
    int fpsRemotePS4() const;
    void setFpsRemotePS4(int fps);
    int fpsLocalPS5() const;
    void setFpsLocalPS5(int fps);
    int fpsRemotePS5() const;
    void setFpsRemotePS5(int fps);

    int bitrateLocalPS4() const;
    void setBitrateLocalPS4(int bitrate);
    int bitrateRemotePS4() const;
    void setBitrateRemotePS4(int bitrate);
    int bitrateLocalPS5() const;
    void setBitrateLocalPS5(int bitrate);
    int bitrateRemotePS5() const;
    void setBitrateRemotePS5(int bitrate);

    int codecLocalPS5() const;
    void setCodecLocalPS5(int codec);
    int codecRemotePS5() const;
    void setCodecRemotePS5(int codec);

    int audioBufferSize() const;
    void setAudioBufferSize(int size);

    QString audioInDevice() const;
    void setAudioInDevice(const QString &device);

    QString audioOutDevice() const;
    void setAudioOutDevice(const QString &device);

    QString decoder() const;
    void setDecoder(const QString &decoder);

    int windowType() const;
    void setWindowType(int type);

    float sZoomFactor() const;
    void setSZoomFactor(float factor);

    int packetLossMax() const;
    void setPacketLossMax(int packet_loss_max);

    int videoPreset() const;
    void setVideoPreset(int preset);

    QString autoConnectMac() const;
    void setAutoConnectMac(const QString &mac);

    uint wifiDroppedNotif() const;
    void setWifiDroppedNotif(uint percent);

    QString psnAuthToken() const;
    void setPsnAuthToken(const QString &auth_token);

    QString psnRefreshToken() const;
    void setPsnRefreshToken(const QString &refresh_token);

    QString psnAuthTokenExpiry() const;
    void setPsnAuthTokenExpiry(const QString &expiry);

    QString psnAccountId() const;
    void setPsnAccountId(const QString &account_id);

    QString logDirectory() const;
    QStringList availableDecoders() const;
    QStringList availableAudioOutDevices() const;
    QStringList availableAudioInDevices() const;
    QVariantList registeredHosts() const;
    QVariantList controllerMapping() const;

    Q_INVOKABLE void deleteRegisteredHost(int index);
    Q_INVOKABLE void refreshAudioDevices();
    Q_INVOKABLE QString changeControllerKey(int button, int key);
    Q_INVOKABLE void clearKeyMapping();
    Q_INVOKABLE void exportSettings(QString fileurl);
    Q_INVOKABLE void importSettings(QString fileurl);

signals:
    void resolutionLocalPS4Changed();
    void resolutionRemotePS4Changed();
    void resolutionLocalPS5Changed();
    void resolutionRemotePS5Changed();
    void disconnectActionChanged();
    void suspendActionChanged();
    void logVerboseChanged();
    void dualSenseChanged();
    void buttonsByPositionChanged();
    void startMicUnmutedChanged();
#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
    void verticalDeckChanged();
    void steamDeckHapticsChanged();
#endif
#ifdef CHIAKI_GUI_ENABLE_SPEEX
    void speechProcessingChanged();
    void noiseSuppressLevelChanged();
    void echoSuppressLevelChanged();
#endif
    void fpsLocalPS4Changed();
    void fpsRemotePS4Changed();
    void fpsLocalPS5Changed();
    void fpsRemotePS5Changed();
    void bitrateLocalPS4Changed();
    void bitrateRemotePS4Changed();
    void bitrateLocalPS5Changed();
    void bitrateRemotePS5Changed();
    void codecLocalPS5Changed();
    void codecRemotePS5Changed();
    void audioBufferSizeChanged();
    void audioOutDeviceChanged();
    void audioInDeviceChanged();
    void wifiDroppedNotifChanged();
    void decoderChanged();
    void windowTypeChanged();
    void sZoomFactorChanged();
    void videoPresetChanged();
    void autoConnectMacChanged();
    void audioDevicesChanged();
    void registeredHostsChanged();
    void psnAuthTokenChanged();
    void psnRefreshTokenChanged();
    void psnAuthTokenExpiryChanged();
    void psnAccountIdChanged();
    void controllerMappingChanged();
    void packetLossMaxChanged();

private:
    Settings *settings = {};
    QStringList audio_in_devices;
    QStringList audio_out_devices;
};
