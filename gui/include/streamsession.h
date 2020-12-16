// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_STREAMSESSION_H
#define CHIAKI_STREAMSESSION_H

#include <chiaki/session.h>
#include <chiaki/opusdecoder.h>

#if CHIAKI_LIB_ENABLE_PI_DECODER
#include <chiaki/pidecoder.h>
#endif

#if CHIAKI_GUI_ENABLE_SETSU
#include <setsu.h>
#endif

#include "videodecoder.h"
#include "exception.h"
#include "sessionlog.h"
#include "controllermanager.h"
#include "settings.h"

#include <QObject>
#include <QImage>
#include <QMouseEvent>
#include <QTimer>

class QAudioOutput;
class QIODevice;
class QKeyEvent;
class Settings;

class ChiakiException: public Exception
{
	public:
		explicit ChiakiException(const QString &msg) : Exception(msg) {};
};

struct StreamSessionConnectInfo
{
	Settings *settings;
	QMap<Qt::Key, int> key_map;
	Decoder decoder;
	HardwareDecodeEngine hw_decode_engine;
	QString audio_out_device;
	uint32_t log_level_mask;
	QString log_file;
	ChiakiTarget target;
	QString host;
	QByteArray regist_key;
	QByteArray morning;
	ChiakiConnectVideoProfile video_profile;
	unsigned int audio_buffer_size;
	bool fullscreen;
	bool enable_keyboard;

	StreamSessionConnectInfo(Settings *settings, ChiakiTarget target, QString host, QByteArray regist_key, QByteArray morning, bool fullscreen);
};

class StreamSession : public QObject
{
	friend class StreamSessionPrivate;

	Q_OBJECT

	private:
		SessionLog log;
		ChiakiSession session;
		ChiakiOpusDecoder opus_decoder;
		bool connected;

		QHash<int, Controller *> controllers;
#if CHIAKI_GUI_ENABLE_SETSU
		Setsu *setsu;
		QMap<QPair<QString, SetsuTrackingId>, uint8_t> setsu_ids;
		ChiakiControllerState setsu_state;
#endif

		ChiakiControllerState keyboard_state;

		VideoDecoder *video_decoder;
#if CHIAKI_LIB_ENABLE_PI_DECODER
		ChiakiPiDecoder *pi_decoder;
#endif

		QAudioDeviceInfo audio_out_device_info;
		unsigned int audio_buffer_size;
		QAudioOutput *audio_output;
		QIODevice *audio_io;

		QMap<Qt::Key, int> key_map;

		void PushAudioFrame(int16_t *buf, size_t samples_count);
		void PushVideoSample(uint8_t *buf, size_t buf_size);
		void Event(ChiakiEvent *event);
#if CHIAKI_GUI_ENABLE_SETSU
		void HandleSetsuEvent(SetsuEvent *event);
#endif

	private slots:
		void InitAudio(unsigned int channels, unsigned int rate);

	public:
		explicit StreamSession(const StreamSessionConnectInfo &connect_info, QObject *parent = nullptr);
		~StreamSession();

		bool IsConnected()	{ return connected; }

		void Start();
		void Stop();
		void GoToBed();

		void SetLoginPIN(const QString &pin);

		QList<Controller *> GetControllers()	{ return controllers.values(); }
		VideoDecoder *GetVideoDecoder()	{ return video_decoder; }
#if CHIAKI_LIB_ENABLE_PI_DECODER
		ChiakiPiDecoder *GetPiDecoder()	{ return pi_decoder; }
#endif

		void HandleKeyboardEvent(QKeyEvent *event);
		void HandleMouseEvent(QMouseEvent *event);

	signals:
		void CurrentImageUpdated();
		void SessionQuit(ChiakiQuitReason reason, const QString &reason_str);
		void LoginPINRequested(bool incorrect);

	private slots:
		void UpdateGamepads();
		void SendFeedbackState();
};

Q_DECLARE_METATYPE(ChiakiQuitReason)

#endif // CHIAKI_STREAMSESSION_H
