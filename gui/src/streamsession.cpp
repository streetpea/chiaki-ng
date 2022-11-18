// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <streamsession.h>
#include <settings.h>
#include <controllermanager.h>

#include <chiaki/base64.h>

#include <QKeyEvent>
#include <QAudioOutput>

#include <cstring>
#include <chiaki/session.h>

#define SETSU_UPDATE_INTERVAL_MS 4
// DualSense is 1919 x 1079, DualShock4 is 1920 x 942 (take highest of both here to map completely to both)
#define PS_TOUCHPAD_MAX_X 1920
#define PS_TOUCHPAD_MAX_Y 1079

StreamSessionConnectInfo::StreamSessionConnectInfo(Settings *settings, ChiakiTarget target, QString host, QByteArray regist_key, QByteArray morning, QString initial_login_pin, bool fullscreen, bool zoom, bool stretch, bool enable_dualsense)
	: settings(settings)
{
	key_map = settings->GetControllerMappingForDecoding();
	decoder = settings->GetDecoder();
	hw_decoder = settings->GetHardwareDecoder();
	audio_out_device = settings->GetAudioOutDevice();
	log_level_mask = settings->GetLogLevelMask();
	log_file = CreateLogFilename();
	video_profile = settings->GetVideoProfile();
	this->target = target;
	this->host = host;
	this->regist_key = regist_key;
	this->morning = morning;
	this->initial_login_pin = initial_login_pin;
	audio_buffer_size = settings->GetAudioBufferSize();
	this->fullscreen = fullscreen;
	this->zoom = zoom;
	this->stretch = stretch;
	this->enable_keyboard = false; // TODO: from settings
	this->enable_dualsense = enable_dualsense;
}

static void AudioSettingsCb(uint32_t channels, uint32_t rate, void *user);
static void AudioFrameCb(int16_t *buf, size_t samples_count, void *user);
static void HapticsFrameCb(uint8_t *buf, size_t buf_size, void *user);
static void EventCb(ChiakiEvent *event, void *user);
#if CHIAKI_GUI_ENABLE_SETSU
static void SessionSetsuCb(SetsuEvent *event, void *user);
#endif
static void FfmpegFrameCb(ChiakiFfmpegDecoder *decoder, void *user);

StreamSession::StreamSession(const StreamSessionConnectInfo &connect_info, QObject *parent)
	: QObject(parent),
	log(this, connect_info.log_level_mask, connect_info.log_file),
	ffmpeg_decoder(nullptr),
#if CHIAKI_LIB_ENABLE_PI_DECODER
	pi_decoder(nullptr),
#endif
	audio_output(nullptr),
	audio_io(nullptr),
	haptics_output(0),
	haptics_resampler_buf(nullptr)
{
	connected = false;
	ChiakiErrorCode err;

#if CHIAKI_LIB_ENABLE_PI_DECODER
	if(connect_info.decoder == Decoder::Pi)
	{
		pi_decoder = CHIAKI_NEW(ChiakiPiDecoder);
		if(chiaki_pi_decoder_init(pi_decoder, log.GetChiakiLog()) != CHIAKI_ERR_SUCCESS)
			throw ChiakiException("Failed to initialize Raspberry Pi Decoder");
	}
	else
	{
#endif
		ffmpeg_decoder = new ChiakiFfmpegDecoder;
		ChiakiLogSniffer sniffer;
		chiaki_log_sniffer_init(&sniffer, CHIAKI_LOG_ALL, GetChiakiLog());
		err = chiaki_ffmpeg_decoder_init(ffmpeg_decoder,
				chiaki_log_sniffer_get_log(&sniffer),
				chiaki_target_is_ps5(connect_info.target) ? connect_info.video_profile.codec : CHIAKI_CODEC_H264,
				connect_info.hw_decoder.isEmpty() ? NULL : connect_info.hw_decoder.toUtf8().constData(),
				FfmpegFrameCb, this);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			QString log = QString::fromUtf8(chiaki_log_sniffer_get_buffer(&sniffer));
			chiaki_log_sniffer_fini(&sniffer);
			throw ChiakiException("Failed to initialize FFMPEG Decoder:\n" + log);
		}
		chiaki_log_sniffer_fini(&sniffer);
		ffmpeg_decoder->log = GetChiakiLog();
#if CHIAKI_LIB_ENABLE_PI_DECODER
	}
#endif

	audio_out_device_info = QAudioDeviceInfo::defaultOutputDevice();
	if(!connect_info.audio_out_device.isEmpty())
	{
		for(QAudioDeviceInfo di : QAudioDeviceInfo::availableDevices(QAudio::AudioOutput))
		{
			if(di.deviceName() == connect_info.audio_out_device)
			{
				audio_out_device_info = di;
				break;
			}
		}
	}

	chiaki_opus_decoder_init(&opus_decoder, log.GetChiakiLog());
	audio_buffer_size = connect_info.audio_buffer_size;

	QByteArray host_str = connect_info.host.toUtf8();

	ChiakiConnectInfo chiaki_connect_info = {};
	chiaki_connect_info.ps5 = chiaki_target_is_ps5(connect_info.target);
	chiaki_connect_info.host = host_str.constData();
	chiaki_connect_info.video_profile = connect_info.video_profile;
	chiaki_connect_info.video_profile_auto_downgrade = true;
	chiaki_connect_info.enable_keyboard = false;
	chiaki_connect_info.enable_dualsense = connect_info.enable_dualsense;

#if CHIAKI_LIB_ENABLE_PI_DECODER
	if(connect_info.decoder == Decoder::Pi && chiaki_connect_info.video_profile.codec != CHIAKI_CODEC_H264)
	{
		CHIAKI_LOGW(GetChiakiLog(), "A codec other than H264 was requested for Pi Decoder. Falling back to it.");
		chiaki_connect_info.video_profile.codec = CHIAKI_CODEC_H264;
	}
#endif

	if(connect_info.regist_key.size() != sizeof(chiaki_connect_info.regist_key))
		throw ChiakiException("RegistKey invalid");
	memcpy(chiaki_connect_info.regist_key, connect_info.regist_key.constData(), sizeof(chiaki_connect_info.regist_key));

	if(connect_info.morning.size() != sizeof(chiaki_connect_info.morning))
		throw ChiakiException("Morning invalid");
	memcpy(chiaki_connect_info.morning, connect_info.morning.constData(), sizeof(chiaki_connect_info.morning));

	chiaki_controller_state_set_idle(&keyboard_state);
	chiaki_controller_state_set_idle(&touch_state);
	touch_tracker=QMap<int, uint8_t>();
	mouse_touch_id=-1;

	err = chiaki_session_init(&session, &chiaki_connect_info, GetChiakiLog());
	if(err != CHIAKI_ERR_SUCCESS)
		throw ChiakiException("Chiaki Session Init failed: " + QString::fromLocal8Bit(chiaki_error_string(err)));

	chiaki_opus_decoder_set_cb(&opus_decoder, AudioSettingsCb, AudioFrameCb, this);
	ChiakiAudioSink audio_sink;
	chiaki_opus_decoder_get_sink(&opus_decoder, &audio_sink);
	chiaki_session_set_audio_sink(&session, &audio_sink);

	if (connect_info.enable_dualsense) {
		ChiakiAudioSink haptics_sink;
		haptics_sink.user = this;
		haptics_sink.frame_cb = HapticsFrameCb;
		chiaki_session_set_haptics_sink(&session, &haptics_sink);
	}

#if CHIAKI_LIB_ENABLE_PI_DECODER
	if(pi_decoder)
		chiaki_session_set_video_sample_cb(&session, chiaki_pi_decoder_video_sample_cb, pi_decoder);
	else
	{
#endif
		chiaki_session_set_video_sample_cb(&session, chiaki_ffmpeg_decoder_video_sample_cb, ffmpeg_decoder);
#if CHIAKI_LIB_ENABLE_PI_DECODER
	}
#endif

	chiaki_session_set_event_cb(&session, EventCb, this);

#if CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	connect(ControllerManager::GetInstance(), &ControllerManager::AvailableControllersUpdated, this, &StreamSession::UpdateGamepads);
#endif

#if CHIAKI_GUI_ENABLE_SETSU
	setsu_motion_device = nullptr;
	chiaki_controller_state_set_idle(&setsu_state);
	setsu_ids=QMap<QPair<QString, SetsuTrackingId>, uint8_t>();
	orient_dirty = true;
	chiaki_orientation_tracker_init(&orient_tracker);
	setsu = setsu_new();
	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this]{
		setsu_poll(setsu, SessionSetsuCb, this);
		if(orient_dirty)
		{
			chiaki_orientation_tracker_apply_to_controller_state(&orient_tracker, &setsu_state);
			SendFeedbackState();
			orient_dirty = false;
		}
	});
	timer->start(SETSU_UPDATE_INTERVAL_MS);
#endif

	key_map = connect_info.key_map;
	UpdateGamepads();
	if (connect_info.enable_dualsense) {
		InitHaptics();
	}
}

StreamSession::~StreamSession()
{
	chiaki_session_join(&session);
	chiaki_session_fini(&session);
	chiaki_opus_decoder_fini(&opus_decoder);
#if CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	for(auto controller : controllers)
		delete controller;
#endif
#if CHIAKI_GUI_ENABLE_SETSU
	setsu_free(setsu);
#endif
#if CHIAKI_LIB_ENABLE_PI_DECODER
	if(pi_decoder)
	{
		chiaki_pi_decoder_fini(pi_decoder);
		free(pi_decoder);
	}
#endif
	if(ffmpeg_decoder)
	{
		chiaki_ffmpeg_decoder_fini(ffmpeg_decoder);
		delete ffmpeg_decoder;
	}
	if (haptics_output > 0)
	{
		SDL_CloseAudioDevice(haptics_output);
		haptics_output = 0;
	}
	if (haptics_resampler_buf)
	{
		free(haptics_resampler_buf);
		haptics_resampler_buf = nullptr;
	}
}

void StreamSession::Start()
{
	ChiakiErrorCode err = chiaki_session_start(&session);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		chiaki_session_fini(&session);
		throw ChiakiException("Chiaki Session Start failed");
	}
}

void StreamSession::Stop()
{
	chiaki_session_stop(&session);
}

void StreamSession::GoToBed()
{
	chiaki_session_goto_bed(&session);
}

void StreamSession::SetLoginPIN(const QString &pin)
{
	QByteArray data = pin.toUtf8();
	chiaki_session_set_login_pin(&session, (const uint8_t *)data.constData(), data.size());
}

void StreamSession::HandleMousePressEvent(QMouseEvent *event)
{
	// left button for touchpad gestures, others => touchpad click
	if (event->button() != Qt::MouseButton::LeftButton)
		keyboard_state.buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
	SendFeedbackState();
}

void StreamSession::HandleMouseReleaseEvent(QMouseEvent *event)
{
	// left button => end of touchpad gesture
	if (event->button() == Qt::LeftButton)
	{
		if(mouse_touch_id >= 0)
		{
			chiaki_controller_state_stop_touch(&keyboard_state, (uint8_t)mouse_touch_id);
			mouse_touch_id = -1;
		}
	}
	keyboard_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
	SendFeedbackState();
}

void StreamSession::HandleMouseMoveEvent(QMouseEvent *event, float width, float height)
{
	// left button with move => touchpad gesture, otherwise ignore
	if (event->buttons() == Qt::LeftButton)
	{
		float x = event->screenPos().x();
		float y = event->screenPos().y();
		float psx = x * ((float)PS_TOUCHPAD_MAX_X / width);
		float psy = y * ((float)PS_TOUCHPAD_MAX_Y / height);
		// if touch id is set, move, otherwise start
		if(mouse_touch_id >= 0)
			chiaki_controller_state_set_touch_pos(&keyboard_state, (uint8_t)mouse_touch_id, (uint16_t)psx, (uint16_t)psy);
		else
			mouse_touch_id=chiaki_controller_state_start_touch(&keyboard_state, (uint16_t)psx, (uint16_t)psy);	
	}
	SendFeedbackState();
}

void StreamSession::HandleKeyboardEvent(QKeyEvent *event)
{
	if(key_map.contains(Qt::Key(event->key())) == false)
		return;

	if(event->isAutoRepeat())
		return;

	int button = key_map[Qt::Key(event->key())];
	bool press_event = event->type() == QEvent::Type::KeyPress;

	switch(button)
	{
		case CHIAKI_CONTROLLER_ANALOG_BUTTON_L2:
			keyboard_state.l2_state = press_event ? 0xff : 0;
			break;
		case CHIAKI_CONTROLLER_ANALOG_BUTTON_R2:
			keyboard_state.r2_state = press_event ? 0xff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y_UP):
			keyboard_state.right_y = press_event ? -0x7fff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y_DOWN):
			keyboard_state.right_y = press_event ? 0x7fff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X_UP):
			keyboard_state.right_x = press_event ? 0x7fff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X_DOWN):
			keyboard_state.right_x = press_event ? -0x7fff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y_UP):
			keyboard_state.left_y = press_event ? -0x7fff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y_DOWN):
			keyboard_state.left_y = press_event ? 0x7fff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X_UP):
			keyboard_state.left_x = press_event ? 0x7fff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X_DOWN):
			keyboard_state.left_x = press_event ? -0x7fff : 0;
			break;
		default:
			if(press_event)
				keyboard_state.buttons |= button;
			else
				keyboard_state.buttons &= ~button;
			break;
	}

	SendFeedbackState();
}

void StreamSession::HandleTouchEvent(QTouchEvent *event)
{
	//unset touchpad (we will set it if user touches edge of screen)
	touch_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;

	const QList<QTouchEvent::TouchPoint> touchPoints = event->touchPoints();

	for (const QTouchEvent::TouchPoint &touchPoint : touchPoints)
	{
		int id = touchPoint.id();
		switch (touchPoint.state())
		{
			//skip unchanged touchpoints
			case Qt::TouchPointStationary:
				continue;
			case Qt::TouchPointPressed:
			case Qt::TouchPointMoved:
			{
				float norm_x = touchPoint.normalizedPos().x();
				float norm_y = touchPoint.normalizedPos().y();
				
				// Touching edges of screen is a touchpad click
				if(norm_x <= 0.05 || norm_x >= 0.95 || norm_y <= 0.05 || norm_y >= 0.95)
					touch_state.buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
				// Scale to PS TouchPad since that's what PS Console expects
				float psx = norm_x * (float)PS_TOUCHPAD_MAX_X;
				float psy = norm_y * (float)PS_TOUCHPAD_MAX_Y;
				auto it = touch_tracker.find(id);
				if(it == touch_tracker.end())
				{
					int8_t cid = chiaki_controller_state_start_touch(&touch_state, (uint16_t)psx, (uint16_t)psy);
					// if cid < 0 => already too many multi-touches
					if(cid >= 0)
						touch_tracker[id] = (uint8_t)cid;
					else
						break;
				}
				else
					chiaki_controller_state_set_touch_pos(&touch_state, it.value(), (uint16_t)psx, (uint16_t)psy);
				break;
			}
			case Qt::TouchPointReleased:
			{
				for(auto it=touch_tracker.begin(); it!=touch_tracker.end(); it++)
				{
					if(it.key() == id)
					{
						chiaki_controller_state_stop_touch(&touch_state, it.value());
						touch_tracker.erase(it);
						break;
					}
				}
				break;
			}
		}
	}
	SendFeedbackState();
}

void StreamSession::UpdateGamepads()
{
#if CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	for(auto controller_id : controllers.keys())
	{
		auto controller = controllers[controller_id];
		if(!controller->IsConnected())
		{
			CHIAKI_LOGI(log.GetChiakiLog(), "Controller %d disconnected", controller->GetDeviceID());
			controllers.remove(controller_id);
			delete controller;
		}
	}

	const auto available_controllers = ControllerManager::GetInstance()->GetAvailableControllers();
	for(auto controller_id : available_controllers)
	{
		if(!controllers.contains(controller_id))
		{
			auto controller = ControllerManager::GetInstance()->OpenController(controller_id);
			if(!controller)
			{
				CHIAKI_LOGE(log.GetChiakiLog(), "Failed to open controller %d", controller_id);
				continue;
			}
			CHIAKI_LOGI(log.GetChiakiLog(), "Controller %d opened: \"%s\"", controller_id, controller->GetName().toLocal8Bit().constData());
			connect(controller, &Controller::StateChanged, this, &StreamSession::SendFeedbackState);
			controllers[controller_id] = controller;
		}
	}
	
	SendFeedbackState();
#endif
}

void StreamSession::SendFeedbackState()
{
	ChiakiControllerState state;
	chiaki_controller_state_set_idle(&state);

#if CHIAKI_GUI_ENABLE_SETSU
	// setsu is the one that potentially has gyro/accel/orient so copy that directly first
	state = setsu_state;
#endif

	for(auto controller : controllers)
	{
		auto controller_state = controller->GetState();
		chiaki_controller_state_or(&state, &state, &controller_state);
	}

	chiaki_controller_state_or(&state, &state, &keyboard_state);
	chiaki_controller_state_or(&state, &state, &touch_state);
	chiaki_session_set_controller_state(&session, &state);
}

void StreamSession::InitAudio(unsigned int channels, unsigned int rate)
{
	delete audio_output;
	audio_output = nullptr;
	audio_io = nullptr;

	QAudioFormat audio_format;
	audio_format.setSampleRate(rate);
	audio_format.setChannelCount(channels);
	audio_format.setSampleSize(16);
	audio_format.setCodec("audio/pcm");
	audio_format.setSampleType(QAudioFormat::SignedInt);

	QAudioDeviceInfo audio_device_info = audio_out_device_info;
	if(!audio_device_info.isFormatSupported(audio_format))
	{
		CHIAKI_LOGE(log.GetChiakiLog(), "Audio Format with %u channels @ %u Hz not supported by Audio Device %s",
					channels, rate,
					audio_device_info.deviceName().toLocal8Bit().constData());
		return;
	}

	audio_output = new QAudioOutput(audio_device_info, audio_format, this);
	audio_output->setBufferSize(audio_buffer_size);
	audio_io = audio_output->start();

	CHIAKI_LOGI(log.GetChiakiLog(), "Audio Device %s opened with %u channels @ %u Hz, buffer size %u",
				audio_device_info.deviceName().toLocal8Bit().constData(),
				channels, rate, audio_output->bufferSize());
}

void StreamSession::InitHaptics()
{
	haptics_output = 0;
	haptics_resampler_buf = nullptr;
	// Haptics work most reliably with Pipewire, so try to use that if available
	SDL_SetHint("SDL_AUDIODRIVER", "pipewire");

	if (SDL_Init(SDL_INIT_AUDIO) < 0)
	{
		CHIAKI_LOGE(log.GetChiakiLog(), "Could not initialize SDL Audio for haptics output: %s", SDL_GetError());
		return;
	}

	if (!strstr(SDL_GetCurrentAudioDriver(), "pipewire")) {
		CHIAKI_LOGW(
			log.GetChiakiLog(),
			"Haptics output is not using Pipewire, this may not work reliably. (was: '%s')",
			SDL_GetCurrentAudioDriver());
	}

	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = 48000;
	want.format = AUDIO_S16LSB;
	want.channels = 4;
	want.samples = 480; // 10ms buffer
	want.callback = NULL;

	const char *device_name = nullptr;
	for (int i=0; i < SDL_GetNumAudioDevices(0); i++) {
		device_name = SDL_GetAudioDeviceName(i, 0);
		if (!device_name || !strstr(device_name, "DualSense")) {
			continue;
		}
		haptics_output = SDL_OpenAudioDevice(device_name, 0, &want, &have, 0);
		if (haptics_output == 0) {
			CHIAKI_LOGE(log.GetChiakiLog(), "Could not open SDL Audio Device %s for haptics output: %s", device_name, SDL_GetError());
			continue;
		}
		SDL_PauseAudioDevice(haptics_output, 0);
		break;
	}
	if(!haptics_output)
	{
		CHIAKI_LOGW(log.GetChiakiLog(), "DualSense features were enabled, but could not find the DualSense audio device!");
		return;
	}
	SDL_AudioCVT cvt;
	SDL_BuildAudioCVT(&cvt, AUDIO_S16LSB, 4, 3000, AUDIO_S16LSB, 4, 48000);
	cvt.len = 240;  // 10 16bit stereo samples
	haptics_resampler_buf = (uint8_t*) calloc(cvt.len * cvt.len_mult, sizeof(uint8_t));

	CHIAKI_LOGI(log.GetChiakiLog(), "Haptics Audio Device '%s' opened with %d channels @ %d Hz, buffer size %u (driver=%s)", device_name, have.channels, have.freq, have.size, SDL_GetCurrentAudioDriver());
}

void StreamSession::PushAudioFrame(int16_t *buf, size_t samples_count)
{
	if(!audio_io)
		return;
	audio_io->write((const char *)buf, static_cast<qint64>(samples_count * 2 * 2));
}

void StreamSession::PushHapticsFrame(uint8_t *buf, size_t buf_size)
{
	if(haptics_output == 0)
		return;
	SDL_AudioCVT cvt;
	// Haptics samples are coming in at 3KHZ, but the DualSense expects 48KHZ
	SDL_BuildAudioCVT(&cvt, AUDIO_S16LSB, 4, 3000, AUDIO_S16LSB, 4, 48000);
	cvt.len = buf_size * 2;
	cvt.buf = haptics_resampler_buf;
	// Remix to 4 channels
	for (int i=0; i < buf_size; i+=4) {
		SDL_memset(haptics_resampler_buf + i * 2, 0, 4);
		SDL_memcpy(haptics_resampler_buf + (i * 2) + 4, buf + i, 4);
	}
	// Resample to 48kHZ
	if (SDL_ConvertAudio(&cvt) != 0) {
		CHIAKI_LOGE(log.GetChiakiLog(), "Failed to resample haptics audio: %s", SDL_GetError());
		return;
	}

	if (SDL_QueueAudio(haptics_output, cvt.buf, cvt.len_cvt) < 0) {
		CHIAKI_LOGE(log.GetChiakiLog(), "Failed to submit haptics audio to device: %s", SDL_GetError());
		return;
	}
}

void StreamSession::Event(ChiakiEvent *event)
{
	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED:
			connected = true;
			break;
		case CHIAKI_EVENT_QUIT:
			connected = false;
			emit SessionQuit(event->quit.reason, event->quit.reason_str ? QString::fromUtf8(event->quit.reason_str) : QString());
			break;
		case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
			emit LoginPINRequested(event->login_pin_request.pin_incorrect);
			break;
		case CHIAKI_EVENT_RUMBLE: {
			uint8_t left = event->rumble.left;
			uint8_t right = event->rumble.right;
			QMetaObject::invokeMethod(this, [this, left, right]() {
				for(auto controller : controllers)
					controller->SetRumble(left, right);
			});
			break;
		}
		case CHIAKI_EVENT_TRIGGER_EFFECTS: {
			uint8_t type_left = event->trigger_effects.type_left;
			uint8_t data_left[10];
			memcpy(data_left, event->trigger_effects.left, 10);
			uint8_t data_right[10];
			memcpy(data_right, event->trigger_effects.right, 10);
			uint8_t type_right = event->trigger_effects.type_right;
			QMetaObject::invokeMethod(this, [this, type_left, data_left, type_right, data_right]() {
				for(auto controller : controllers)
					controller->SetTriggerEffects(type_left, data_left, type_right, data_right);
			});
			break;
		}
		default:
			break;
	}
}

#if CHIAKI_GUI_ENABLE_SETSU
void StreamSession::HandleSetsuEvent(SetsuEvent *event)
{
	if(!setsu)
		return;
	switch(event->type)
	{
		case SETSU_EVENT_DEVICE_ADDED:
			switch(event->dev_type)
			{
				case SETSU_DEVICE_TYPE_TOUCHPAD:
					// connect all the touchpads!
					if(setsu_connect(setsu, event->path, event->dev_type))
						CHIAKI_LOGI(GetChiakiLog(), "Connected Setsu Touchpad Device %s", event->path);
					else
						CHIAKI_LOGE(GetChiakiLog(), "Failed to connect to Setsu Touchpad Device %s", event->path);
					break;
				case SETSU_DEVICE_TYPE_MOTION:
					// connect only one motion since multiple make no sense
					if(setsu_motion_device)
					{
						CHIAKI_LOGI(GetChiakiLog(), "Setsu Motion Device %s detected there is already one connected",
								event->path);
						break;
					}
					setsu_motion_device = setsu_connect(setsu, event->path, event->dev_type);
					if(setsu_motion_device)
						CHIAKI_LOGI(GetChiakiLog(), "Connected Setsu Motion Device %s", event->path);
					else
						CHIAKI_LOGE(GetChiakiLog(), "Failed to connect to Setsu Motion Device %s", event->path);
					break;
			}
			break;
		case SETSU_EVENT_DEVICE_REMOVED:
			switch(event->dev_type)
			{
				case SETSU_DEVICE_TYPE_TOUCHPAD:
					CHIAKI_LOGI(GetChiakiLog(), "Setsu Touchpad Device %s disconnected", event->path);
					for(auto it=setsu_ids.begin(); it!=setsu_ids.end();)
					{
						if(it.key().first == event->path)
						{
							chiaki_controller_state_stop_touch(&setsu_state, it.value());
							setsu_ids.erase(it++);
						}
						else
							it++;
					}
					SendFeedbackState();
					break;
				case SETSU_DEVICE_TYPE_MOTION:
					if(!setsu_motion_device || strcmp(setsu_device_get_path(setsu_motion_device), event->path))
						break;
					CHIAKI_LOGI(GetChiakiLog(), "Setsu Motion Device %s disconnected", event->path);
					setsu_motion_device = nullptr;
					chiaki_orientation_tracker_init(&orient_tracker);
					orient_dirty = true;
					break;
			}
			break;
		case SETSU_EVENT_TOUCH_DOWN:
			break;
		case SETSU_EVENT_TOUCH_UP:
			for(auto it=setsu_ids.begin(); it!=setsu_ids.end(); it++)
			{
				if(it.key().first == setsu_device_get_path(event->dev) && it.key().second == event->touch.tracking_id)
				{
					chiaki_controller_state_stop_touch(&setsu_state, it.value());
					setsu_ids.erase(it);
					break;
				}
			}
			SendFeedbackState();
			break;
		case SETSU_EVENT_TOUCH_POSITION: {
			QPair<QString, SetsuTrackingId> k =  { setsu_device_get_path(event->dev), event->touch.tracking_id };
			auto it = setsu_ids.find(k);
			if(it == setsu_ids.end())
			{
				int8_t cid = chiaki_controller_state_start_touch(&setsu_state, event->touch.x, event->touch.y);
				if(cid >= 0)
					setsu_ids[k] = (uint8_t)cid;
				else
					break;
			}
			else
				chiaki_controller_state_set_touch_pos(&setsu_state, it.value(), event->touch.x, event->touch.y);
			SendFeedbackState();
			break;
		}
		case SETSU_EVENT_BUTTON_DOWN:
			setsu_state.buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
			break;
		case SETSU_EVENT_BUTTON_UP:
			setsu_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
			break;
		case SETSU_EVENT_MOTION:
			chiaki_orientation_tracker_update(&orient_tracker,
					event->motion.gyro_x, event->motion.gyro_y, event->motion.gyro_z,
					event->motion.accel_x, event->motion.accel_y, event->motion.accel_z,
					event->motion.timestamp);
			orient_dirty = true;
			break;
	}
}
#endif

void StreamSession::TriggerFfmpegFrameAvailable()
{
	emit FfmpegFrameAvailable();
}

class StreamSessionPrivate
{
	public:
		static void InitAudio(StreamSession *session, uint32_t channels, uint32_t rate)
		{
			QMetaObject::invokeMethod(session, "InitAudio", Qt::ConnectionType::BlockingQueuedConnection, Q_ARG(unsigned int, channels), Q_ARG(unsigned int, rate));
		}

		static void PushAudioFrame(StreamSession *session, int16_t *buf, size_t samples_count)	{ session->PushAudioFrame(buf, samples_count); }
		static void PushHapticsFrame(StreamSession *session, uint8_t *buf, size_t buf_size)	{ session->PushHapticsFrame(buf, buf_size); }
		static void Event(StreamSession *session, ChiakiEvent *event)							{ session->Event(event); }
#if CHIAKI_GUI_ENABLE_SETSU
		static void HandleSetsuEvent(StreamSession *session, SetsuEvent *event)					{ session->HandleSetsuEvent(event); }
#endif
		static void TriggerFfmpegFrameAvailable(StreamSession *session)							{ session->TriggerFfmpegFrameAvailable(); }
};

static void AudioSettingsCb(uint32_t channels, uint32_t rate, void *user)
{
	auto session = reinterpret_cast<StreamSession *>(user);
	StreamSessionPrivate::InitAudio(session, channels, rate);
}

static void AudioFrameCb(int16_t *buf, size_t samples_count, void *user)
{
	auto session = reinterpret_cast<StreamSession *>(user);
	StreamSessionPrivate::PushAudioFrame(session, buf, samples_count);
}

static void HapticsFrameCb(uint8_t *buf, size_t buf_size, void *user)
{
	auto session = reinterpret_cast<StreamSession *>(user);
	StreamSessionPrivate::PushHapticsFrame(session, buf, buf_size);
}

static void EventCb(ChiakiEvent *event, void *user)
{
	auto session = reinterpret_cast<StreamSession *>(user);
	StreamSessionPrivate::Event(session, event);
}

#if CHIAKI_GUI_ENABLE_SETSU
static void SessionSetsuCb(SetsuEvent *event, void *user)
{
	auto session = reinterpret_cast<StreamSession *>(user);
	StreamSessionPrivate::HandleSetsuEvent(session, event);
}
#endif

static void FfmpegFrameCb(ChiakiFfmpegDecoder *decoder, void *user)
{
	auto session = reinterpret_cast<StreamSession *>(user);
	StreamSessionPrivate::TriggerFfmpegFrameAvailable(session);
}
