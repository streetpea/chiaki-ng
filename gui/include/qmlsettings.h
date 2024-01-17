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
    Q_PROPERTY(bool buttonsByPosition READ buttonsByPosition WRITE setButtonsByPosition NOTIFY buttonsByPositionChanged)
    Q_PROPERTY(bool verticalDeck READ verticalDeck WRITE setVerticalDeck NOTIFY verticalDeckChanged)
    Q_PROPERTY(bool speechProcessing READ speechProcessing WRITE setSpeechProcessing NOTIFY speechProcessingChanged)
    Q_PROPERTY(int noiseSuppressLevel READ noiseSuppressLevel WRITE setNoiseSuppressLevel NOTIFY noiseSuppressLevelChanged)
    Q_PROPERTY(int echoSuppressLevel READ echoSuppressLevel WRITE setEchoSuppressLevel NOTIFY echoSuppressLevelChanged)
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)
    Q_PROPERTY(int bitrate READ bitrate WRITE setBitrate NOTIFY bitrateChanged)
    Q_PROPERTY(int codec READ codec WRITE setCodec NOTIFY codecChanged)
    Q_PROPERTY(int audioBufferSize READ audioBufferSize WRITE setAudioBufferSize NOTIFY audioBufferSizeChanged)
    Q_PROPERTY(QString audioInDevice READ audioInDevice WRITE setAudioInDevice NOTIFY audioInDeviceChanged)
    Q_PROPERTY(QString audioOutDevice READ audioOutDevice WRITE setAudioOutDevice NOTIFY audioOutDeviceChanged)
    Q_PROPERTY(QString decoder READ decoder WRITE setDecoder NOTIFY decoderChanged)
    Q_PROPERTY(int videoPreset READ videoPreset WRITE setVideoPreset NOTIFY videoPresetChanged)
    Q_PROPERTY(QString autoConnectMac READ autoConnectMac WRITE setAutoConnectMac NOTIFY autoConnectMacChanged)
    Q_PROPERTY(QString logDirectory READ logDirectory CONSTANT)
    Q_PROPERTY(QStringList availableDecoders READ availableDecoders CONSTANT)
    Q_PROPERTY(QStringList availableAudioInDevices READ availableAudioInDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(QStringList availableAudioOutDevices READ availableAudioOutDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(QVariantList registeredHosts READ registeredHosts NOTIFY registeredHostsChanged)
    Q_PROPERTY(QVariantList controllerMapping READ controllerMapping CONSTANT)

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

    bool buttonsByPosition() const;
    void setButtonsByPosition(bool buttonsByPosition);

    bool verticalDeck() const;
    void setVerticalDeck(bool vertical);

    bool speechProcessing() const;
    void setSpeechProcessing(bool processing);

    int noiseSuppressLevel() const;
    void setNoiseSuppressLevel(int level);

    int echoSuppressLevel() const;
    void setEchoSuppressLevel(int level);

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

    int videoPreset() const;
    void setVideoPreset(int preset);

    QString autoConnectMac() const;
    void setAutoConnectMac(const QString &mac);

    QString logDirectory() const;
    QStringList availableDecoders() const;
    QStringList availableAudioOutDevices() const;
    QStringList availableAudioInDevices() const;
    QVariantList registeredHosts() const;
    QVariantList controllerMapping() const;

    Q_INVOKABLE void deleteRegisteredHost(int index);
    Q_INVOKABLE void refreshAudioDevices();
    Q_INVOKABLE QString changeControllerKey(int button, int key);

signals:
    void resolutionChanged();
    void disconnectActionChanged();
    void suspendActionChanged();
    void logVerboseChanged();
    void dualSenseChanged();
    void buttonsByPositionChanged();
    void verticalDeckChanged();
    void speechProcessingChanged();
    void noiseSuppressLevelChanged();
    void echoSuppressLevelChanged();
    void fpsChanged();
    void bitrateChanged();
    void codecChanged();
    void audioBufferSizeChanged();
    void audioOutDeviceChanged();
    void audioInDeviceChanged();
    void decoderChanged();
    void videoPresetChanged();
    void autoConnectMacChanged();
    void audioDevicesChanged();
    void registeredHostsChanged();

private:
    Settings *settings = {};
    QStringList audio_in_devices;
    QStringList audio_out_devices;
};
