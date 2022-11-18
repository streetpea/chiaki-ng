// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_STREAMSESSION_H
#define CHIAKI_STREAMSESSION_H

#include <chiaki/session.h>
#include <chiaki/opusdecoder.h>
#include <chiaki/ffmpegdecoder.h>

#if CHIAKI_LIB_ENABLE_PI_DECODER
#include <chiaki/pidecoder.h>
#endif

#if CHIAKI_GUI_ENABLE_SETSU
#include <setsu.h>
#include <chiaki/orientation.h>
#endif

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
	QString hw_decoder;
	QString audio_out_device;
	uint32_t log_level_mask;
	QString log_file;
	ChiakiTarget target;
	QString host;
	QByteArray regist_key;
	QByteArray morning;
	QString initial_login_pin;
	ChiakiConnectVideoProfile video_profile;
	unsigned int audio_buffer_size;
	bool fullscreen;
	bool zoom;
	bool stretch;
	bool enable_keyboard;
	bool enable_dualsense;

	StreamSessionConnectInfo(Settings *settings, ChiakiTarget target, QString host, QByteArray regist_key, QByteArray morning, QString initial_login_pin, bool fullscreen, bool zoom, bool stretch, bool enable_dualsense);
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
		SetsuDevice *setsu_motion_device;
		ChiakiOrientationTracker orient_tracker;
		bool orient_dirty;
#endif

		ChiakiControllerState keyboard_state;
		ChiakiControllerState touch_state;
		QMap<int, uint8_t> touch_tracker;
		int8_t mouse_touch_id;

		ChiakiFfmpegDecoder *ffmpeg_decoder;
		void TriggerFfmpegFrameAvailable();
#if CHIAKI_LIB_ENABLE_PI_DECODER
		ChiakiPiDecoder *pi_decoder;
#endif

		QAudioDeviceInfo audio_out_device_info;
		unsigned int audio_buffer_size;
		QAudioOutput *audio_output;
		QIODevice *audio_io;
		SDL_AudioDeviceID haptics_output;
		uint8_t *haptics_resampler_buf;

		QMap<Qt::Key, int> key_map;

		void PushAudioFrame(int16_t *buf, size_t samples_count);
		void PushHapticsFrame(uint8_t *buf, size_t buf_size);
#if CHIAKI_GUI_ENABLE_SETSU
		void HandleSetsuEvent(SetsuEvent *event);
#endif

	private slots:
		void InitAudio(unsigned int channels, unsigned int rate);
		void InitHaptics();
		void Event(ChiakiEvent *event);

	public:
		explicit StreamSession(const StreamSessionConnectInfo &connect_info, QObject *parent = nullptr);
		~StreamSession();

		bool IsConnected()	{ return connected; }

		void Start();
		void Stop();
		void GoToBed();

		void SetLoginPIN(const QString &pin);

		ChiakiLog *GetChiakiLog()				{ return log.GetChiakiLog(); }
		QList<Controller *> GetControllers()	{ return controllers.values(); }
		ChiakiFfmpegDecoder *GetFfmpegDecoder()	{ return ffmpeg_decoder; }
#if CHIAKI_LIB_ENABLE_PI_DECODER
		ChiakiPiDecoder *GetPiDecoder()	{ return pi_decoder; }
#endif

		void HandleKeyboardEvent(QKeyEvent *event);
		void HandleTouchEvent(QTouchEvent *event);
		void HandleMouseReleaseEvent(QMouseEvent *event);
		void HandleMousePressEvent(QMouseEvent *event);
		void HandleMouseMoveEvent(QMouseEvent *event, float width, float height);

	signals:
		void FfmpegFrameAvailable();
		void SessionQuit(ChiakiQuitReason reason, const QString &reason_str);
		void LoginPINRequested(bool incorrect);

	private slots:
		void UpdateGamepads();
		void SendFeedbackState();
};

Q_DECLARE_METATYPE(ChiakiQuitReason)

#endif // CHIAKI_STREAMSESSION_H
