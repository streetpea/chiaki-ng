#pragma once

#include "settings.h"

class QmlSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int resolution READ resolution WRITE setResolution NOTIFY resolutionChanged)
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
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)
    Q_PROPERTY(int bitrate READ bitrate WRITE setBitrate NOTIFY bitrateChanged)
    Q_PROPERTY(int codec READ codec WRITE setCodec NOTIFY codecChanged)
    Q_PROPERTY(int audioBufferSize READ audioBufferSize WRITE setAudioBufferSize NOTIFY audioBufferSizeChanged)
    Q_PROPERTY(QString audioInDevice READ audioInDevice WRITE setAudioInDevice NOTIFY audioInDeviceChanged)
    Q_PROPERTY(QString audioOutDevice READ audioOutDevice WRITE setAudioOutDevice NOTIFY audioOutDeviceChanged)
    Q_PROPERTY(QString decoder READ decoder WRITE setDecoder NOTIFY decoderChanged)
    Q_PROPERTY(int windowType READ windowType WRITE setWindowType NOTIFY windowTypeChanged)
    Q_PROPERTY(int videoPreset READ videoPreset WRITE setVideoPreset NOTIFY videoPresetChanged)
    Q_PROPERTY(float sZoomFactor READ sZoomFactor WRITE setSZoomFactor NOTIFY sZoomFactorChanged)
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

    int resolution() const;
    void setResolution(int resolution);

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

    int fps() const;
    void setFps(int fps);

    int bitrate() const;
    void setBitrate(int bitrate);

    int codec() const;
    void setCodec(int codec);

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

signals:
    void resolutionChanged();
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
    void fpsChanged();
    void bitrateChanged();
    void codecChanged();
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

private:
    Settings *settings = {};
    QStringList audio_in_devices;
    QStringList audio_out_devices;
};
