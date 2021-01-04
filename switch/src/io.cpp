// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifdef __SWITCH__
#include <switch.h>
#else
#include <iostream>
#endif

#include "io.h"
#include "settings.h"

// https://github.com/torvalds/linux/blob/41ba50b0572e90ed3d24fe4def54567e9050bc47/drivers/hid/hid-sony.c#L2742
#define DS4_TRACKPAD_MAX_X 1920
#define DS4_TRACKPAD_MAX_Y 942
#define SWITCH_TOUCHSCREEN_MAX_X 1280
#define SWITCH_TOUCHSCREEN_MAX_Y 720

// source:
// gui/src/avopenglwidget.cpp
//
// examples :
// https://www.roxlu.com/2014/039/decoding-h264-and-yuv420p-playback
// https://gist.github.com/roxlu/9329339

// use OpenGl to decode YUV
// the aim is to spare CPU load on nintendo switch

static const char *shader_vert_glsl = R"glsl(
#version 150 core
in vec2 pos_attr;
out vec2 uv_var;
void main()
{
	uv_var = pos_attr;
	gl_Position = vec4(pos_attr * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
}
)glsl";

static const char *yuv420p_shader_frag_glsl = R"glsl(
#version 150 core
uniform sampler2D plane1; // Y
uniform sampler2D plane2; // U
uniform sampler2D plane3; // V
in vec2 uv_var;
out vec4 out_color;
void main()
{
	vec3 yuv = vec3(
		(texture(plane1, uv_var).r - (16.0 / 255.0)) / ((235.0 - 16.0) / 255.0),
		(texture(plane2, uv_var).r - (16.0 / 255.0)) / ((240.0 - 16.0) / 255.0) - 0.5,
		(texture(plane3, uv_var).r - (16.0 / 255.0)) / ((240.0 - 16.0) / 255.0) - 0.5);
	vec3 rgb = mat3(
		1.0,		1.0,		1.0,
		0.0,		-0.21482,	2.12798,
		1.28033,	-0.38059,	0.0) * yuv;
	out_color = vec4(rgb, 1.0);
}
)glsl";

static const float vert_pos[] = {
	0.0f, 0.0f,
	0.0f, 1.0f,
	1.0f, 0.0f,
	1.0f, 1.0f};

IO *IO::instance = nullptr;

IO *IO::GetInstance()
{
	if(instance == nullptr)
	{
		instance = new IO;
	}
	return instance;
}

IO::IO()
{
	Settings *settings = Settings::GetInstance();
	this->log = settings->GetLogger();
}

IO::~IO()
{
	//FreeJoystick();
	if(this->sdl_audio_device_id <= 0)
	{
		SDL_CloseAudioDevice(this->sdl_audio_device_id);
	}
	FreeVideo();
}

void IO::SetMesaConfig()
{
	//TRACE("%s", "Mesaconfig");
	//setenv("MESA_GL_VERSION_OVERRIDE", "3.3", 1);
	//setenv("MESA_GLSL_VERSION_OVERRIDE", "330", 1);
	// Uncomment below to disable error checking and save CPU time (useful for production):
	//setenv("MESA_NO_ERROR", "1", 1);
#ifdef DEBUG_OPENGL
	// Uncomment below to enable Mesa logging:
	setenv("EGL_LOG_LEVEL", "debug", 1);
	setenv("MESA_VERBOSE", "all", 1);
	setenv("NOUVEAU_MESA_DEBUG", "1", 1);

	// Uncomment below to enable shader debugging in Nouveau:
	//setenv("NV50_PROG_OPTIMIZE", "0", 1);
	setenv("NV50_PROG_DEBUG", "1", 1);
	//setenv("NV50_PROG_CHIPSET", "0x120", 1);
#endif
}

#ifdef DEBUG_OPENGL
#define D(x)                                        \
	{                                               \
		(x);                                        \
		CheckGLError(__func__, __FILE__, __LINE__); \
	}
void IO::CheckGLError(const char *func, const char *file, int line)
{
	GLenum err;
	while((err = glGetError()) != GL_NO_ERROR)
	{
		CHIAKI_LOGE(this->log, "glGetError: %x function: %s from %s line %d", err, func, file, line);
		//GL_INVALID_VALUE, 0x0501
		// Given when a value parameter is not a legal value for that function. T
		// his is only given for local problems;
		// if the spec allows the value in certain circumstances,
		// where other parameters or state dictate those circumstances,
		// then GL_INVALID_OPERATION is the result instead.
	}
}

#define DS(x)                                             \
	{                                                     \
		DumpShaderError(x, __func__, __FILE__, __LINE__); \
	}
void IO::DumpShaderError(GLuint shader, const char *func, const char *file, int line)
{
	GLchar str[512 + 1];
	GLsizei len = 0;
	glGetShaderInfoLog(shader, 512, &len, str);
	if(len > 512)
		len = 512;
	str[len] = '\0';
	CHIAKI_LOGE(this->log, "glGetShaderInfoLog: %s function: %s from %s line %d", str, func, file, line);
}

#define DP(x)                                              \
	{                                                      \
		DumpProgramError(x, __func__, __FILE__, __LINE__); \
	}
void IO::DumpProgramError(GLuint prog, const char *func, const char *file, int line)
{
	GLchar str[512 + 1];
	GLsizei len = 0;
	glGetProgramInfoLog(prog, 512, &len, str);
	if(len > 512)
		len = 512;
	str[len] = '\0';
	CHIAKI_LOGE(this->log, "glGetProgramInfoLog: %s function: %s from %s line %d", str, func, file, line);
}

#else
// do nothing
#define D(x) \
	{        \
		(x); \
	}
#define DS(x) \
	{         \
	}
#define DP(x) \
	{         \
	}
#endif

bool IO::VideoCB(uint8_t *buf, size_t buf_size)
{
	// callback function to decode video buffer

	AVPacket packet;
	av_init_packet(&packet);
	packet.data = buf;
	packet.size = buf_size;
	AVFrame *frame = av_frame_alloc();
	if(!frame)
	{
		CHIAKI_LOGE(this->log, "UpdateFrame Failed to alloc AVFrame");
		av_packet_unref(&packet);
		return false;
	}

send_packet:
	// Push
	int r = avcodec_send_packet(this->codec_context, &packet);
	if(r != 0)
	{
		if(r == AVERROR(EAGAIN))
		{
			CHIAKI_LOGE(this->log, "AVCodec internal buffer is full removing frames before pushing");
			r = avcodec_receive_frame(this->codec_context, frame);
			// send decoded frame for sdl texture update
			if(r != 0)
			{
				CHIAKI_LOGE(this->log, "Failed to pull frame");
				av_frame_free(&frame);
				av_packet_unref(&packet);
				return false;
			}
			goto send_packet;
		}
		else
		{
			char errbuf[128];
			av_make_error_string(errbuf, sizeof(errbuf), r);
			CHIAKI_LOGE(this->log, "Failed to push frame: %s", errbuf);
			av_frame_free(&frame);
			av_packet_unref(&packet);
			return false;
		}
	}

	this->mtx.lock();
	// Pull
	r = avcodec_receive_frame(this->codec_context, this->frame);
	this->mtx.unlock();

	if(r != 0)
		CHIAKI_LOGE(this->log, "Failed to pull frame");

	av_frame_free(&frame);
	av_packet_unref(&packet);
	return true;
}

void IO::InitAudioCB(unsigned int channels, unsigned int rate)
{
	SDL_AudioSpec want, have, test;
	SDL_memset(&want, 0, sizeof(want));

	//source
	//[I] Audio Header:
	//[I]   channels = 2
	//[I]   bits = 16
	//[I]   rate = 48000
	//[I]   frame size = 480
	//[I]   unknown = 1
	want.freq = rate;
	want.format = AUDIO_S16SYS;
	// 2 == stereo
	want.channels = channels;
	want.samples = 1024;
	want.callback = NULL;

	if(this->sdl_audio_device_id <= 0)
	{
		// the chiaki session might be called many times
		// open the audio device only once
		this->sdl_audio_device_id = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
	}

	if(this->sdl_audio_device_id <= 0)
	{
		CHIAKI_LOGE(this->log, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
	}
	else
	{
		SDL_PauseAudioDevice(this->sdl_audio_device_id, 0);
	}
}

void IO::AudioCB(int16_t *buf, size_t samples_count)
{
	for(int x = 0; x < samples_count * 2; x++)
	{
		// boost audio volume
		int sample = buf[x] * 1.80;
		// Hard clipping (audio compression)
		// truncate value that overflow/underflow int16
		if(sample > INT16_MAX)
		{
			buf[x] = INT16_MAX;
			CHIAKI_LOGD(this->log, "Audio Hard clipping INT16_MAX < %d", sample);
		}
		else if(sample < INT16_MIN)
		{
			buf[x] = INT16_MIN;
			CHIAKI_LOGD(this->log, "Audio Hard clipping INT16_MIN > %d", sample);
		}
		else
			buf[x] = (int16_t)sample;
	}

	int audio_queued_size = SDL_GetQueuedAudioSize(this->sdl_audio_device_id);
	if(audio_queued_size > 16000)
	{
		// clear audio queue to avoid big audio delay
		// average values are close to 13000 bytes
		CHIAKI_LOGW(this->log, "Triggering SDL_ClearQueuedAudio with queue size = %d", audio_queued_size);
		SDL_ClearQueuedAudio(this->sdl_audio_device_id);
	}

	int success = SDL_QueueAudio(this->sdl_audio_device_id, buf, sizeof(int16_t) * samples_count * 2);
	if(success != 0)
		CHIAKI_LOGE(this->log, "SDL_QueueAudio failed: %s\n", SDL_GetError());
}

bool IO::InitVideo(int video_width, int video_height, int screen_width, int screen_height)
{
	CHIAKI_LOGV(this->log, "load InitVideo");
	this->video_width = video_width;
	this->video_height = video_height;

	this->screen_width = screen_width;
	this->screen_height = screen_height;
	this->frame = av_frame_alloc();

	if(!InitAVCodec())
	{
		throw Exception("Failed to initiate libav codec");
	}

	if(!InitOpenGl())
	{
		throw Exception("Failed to initiate OpenGl");
	}
	return true;
}

bool IO::FreeVideo()
{
	bool ret = true;

	if(this->frame)
		av_frame_free(&this->frame);

	// avcodec_alloc_context3(codec);
	if(this->codec_context)
	{
		avcodec_close(this->codec_context);
		avcodec_free_context(&this->codec_context);
	}

	return ret;
}

bool IO::ReadUserKeyboard(char *buffer, size_t buffer_size)
{
#ifndef __SWITCH__
	// use cin to get user input from linux
	std::cin.getline(buffer, buffer_size);
	CHIAKI_LOGI(this->log, "Got user input: %s\n", buffer);
#else
	// https://kvadevack.se/post/nintendo-switch-virtual-keyboard/
	SwkbdConfig kbd;
	Result rc = swkbdCreate(&kbd, 0);

	if(R_SUCCEEDED(rc))
	{
		swkbdConfigMakePresetDefault(&kbd);
		rc = swkbdShow(&kbd, buffer, buffer_size);

		if(R_SUCCEEDED(rc))
		{
			CHIAKI_LOGI(this->log, "Got user input: %s\n", buffer);
		}
		else
		{
			CHIAKI_LOGE(this->log, "swkbdShow() error: %u\n", rc);
			return false;
		}
		swkbdClose(&kbd);
	}
	else
	{
		CHIAKI_LOGE(this->log, "swkbdCreate() error: %u\n", rc);
		return false;
	}
#endif
	return true;
}

bool IO::ReadGameTouchScreen(ChiakiControllerState *state)
{
#ifdef __SWITCH__
	hidScanInput();
	int touch_count = hidTouchCount();
	bool ret = false;
	if(!touch_count)
	{
		for(int i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
		{
			if(state->touches[i].id != -1)
			{
				state->touches[i].x = 0;
				state->touches[i].y = 0;
				state->touches[i].id = -1;
				state->buttons &= ~CHIAKI_CONTROLLER_BUTTON_TOUCHPAD; // touchscreen release
				// the state changed
				ret = true;
			}
		}
		return ret;
	}

	touchPosition touch;
	for(int i = 0; i < touch_count && i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		hidTouchRead(&touch, i);

		// 1280×720 px (16:9)
		// ps4 controller aspect ratio looks closer to 29:10
		uint16_t x = touch.px * (DS4_TRACKPAD_MAX_X / SWITCH_TOUCHSCREEN_MAX_X);
		uint16_t y = touch.py * (DS4_TRACKPAD_MAX_Y / SWITCH_TOUCHSCREEN_MAX_Y);

		// use nintendo switch border's 5% to
		if(x <= (SWITCH_TOUCHSCREEN_MAX_X * 0.05) || x >= (SWITCH_TOUCHSCREEN_MAX_X * 0.95) || y <= (SWITCH_TOUCHSCREEN_MAX_Y * 0.05) || y >= (SWITCH_TOUCHSCREEN_MAX_Y * 0.95))
		{
			state->buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD; // touchscreen
																 // printf("CHIAKI_CONTROLLER_BUTTON_TOUCHPAD\n");
		}
		else
		{
			state->buttons &= ~CHIAKI_CONTROLLER_BUTTON_TOUCHPAD; // touchscreen release
		}

		state->touches[i].x = x;
		state->touches[i].y = y;
		state->touches[i].id = i;
		// printf("[point_id=%d] px=%03d, py=%03d, dx=%03d, dy=%03d, angle=%03d\n",
		// i, touch.px, touch.py, touch.dx, touch.dy, touch.angle);
		ret = true;
	}
	return ret;
#else
	return false;
#endif
}

bool IO::ReadGameKeys(SDL_Event *event, ChiakiControllerState *state)
{
	// return true if an event changed (gamepad input)

	// TODO
	// share vs PS button
	// Gyro ?
	// rumble ?
	bool ret = true;
	switch(event->type)
	{
		case SDL_JOYAXISMOTION:
			// printf("SDL_JOYAXISMOTION jaxis %d axis %d value %d\n",
			// event->jaxis.which, event->jaxis.axis, event->jaxis.value);
			if(event->jaxis.which == 0)
			{
				// left joystick
				if(event->jaxis.axis == 0)
					// Left-right movement
					state->left_x = event->jaxis.value;
				else if(event->jaxis.axis == 1)
					// Up-Down movement
					state->left_y = event->jaxis.value;
				else if(event->jaxis.axis == 2)
					// Left-right movement
					state->right_x = event->jaxis.value;
				else if(event->jaxis.axis == 3)
					// Up-Down movement
					state->right_y = event->jaxis.value;
				else
					ret = false;
			}
			else if(event->jaxis.which == 1)
			{
				// right joystick
				if(event->jaxis.axis == 0)
					// Left-right movement
					state->right_x = event->jaxis.value;
				else if(event->jaxis.axis == 1)
					// Up-Down movement
					state->right_y = event->jaxis.value;
				else
					ret = false;
			}
			else
				ret = false;
			break;
		case SDL_JOYBUTTONDOWN:
			// printf("Joystick %d button %d DOWN\n",
			// 	event->jbutton.which, event->jbutton.button);
			switch(event->jbutton.button)
			{
				case 0:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_MOON;
					break; // KEY_A
				case 1:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_CROSS;
					break; // KEY_B
				case 2:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_PYRAMID;
					break; // KEY_X
				case 3:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_BOX;
					break; // KEY_Y
				case 12:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
					break; // KEY_DLEFT
				case 14:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
					break; // KEY_DRIGHT
				case 13:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
					break; // KEY_DUP
				case 15:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
					break; // KEY_DDOWN
				case 6:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_L1;
					break; // KEY_L
				case 7:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_R1;
					break; // KEY_R
				case 8:
					state->l2_state = 0xff;
					break; // KEY_ZL
				case 9:
					state->r2_state = 0xff;
					break; // KEY_ZR
				case 4:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_L3;
					break; // KEY_LSTICK
				case 5:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_R3;
					break; // KEY_RSTICK
				case 10:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
					break; // KEY_PLUS
				// FIXME
				// case 11: state->buttons |= CHIAKI_CONTROLLER_BUTTON_SHARE; break; // KEY_MINUS
				case 11:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_PS;
					break; // KEY_MINUS
				default:
					ret = false;
			}
			break;
		case SDL_JOYBUTTONUP:
			// printf("Joystick %d button %d UP\n",
			// 	event->jbutton.which, event->jbutton.button);
			switch(event->jbutton.button)
			{
				case 0:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_MOON;
					break; // KEY_A
				case 1:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_CROSS;
					break; // KEY_B
				case 2:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_PYRAMID;
					break; // KEY_X
				case 3:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_BOX;
					break; // KEY_Y
				case 12:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
					break; // KEY_DLEFT
				case 14:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
					break; // KEY_DRIGHT
				case 13:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
					break; // KEY_DUP
				case 15:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
					break; // KEY_DDOWN
				case 6:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_L1;
					break; // KEY_L
				case 7:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_R1;
					break; // KEY_R
				case 8:
					state->l2_state = 0x00;
					break; // KEY_ZL
				case 9:
					state->r2_state = 0x00;
					break; // KEY_ZR
				case 4:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_L3;
					break; // KEY_LSTICK
				case 5:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_R3;
					break; // KEY_RSTICK
				case 10:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
					break; // KEY_PLUS
						   //case 11: state->buttons ^= CHIAKI_CONTROLLER_BUTTON_SHARE; break; // KEY_MINUS
				case 11:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_PS;
					break; // KEY_MINUS
				default:
					ret = false;
			}
			break;
		default:
			ret = false;
	}
	return ret;
}

bool IO::InitAVCodec()
{
	CHIAKI_LOGV(this->log, "loading AVCodec");
	// set libav video context
	this->codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if(!this->codec)
		throw Exception("H264 Codec not available");

	this->codec_context = avcodec_alloc_context3(codec);
	if(!this->codec_context)
		throw Exception("Failed to alloc codec context");

	// use rock88's mooxlight-nx optimization
	// https://github.com/rock88/moonlight-nx/blob/698d138b9fdd4e483c998254484ccfb4ec829e95/src/streaming/ffmpeg/FFmpegVideoDecoder.cpp#L63
	// this->codec_context->skip_loop_filter = AVDISCARD_ALL;
	this->codec_context->flags |= AV_CODEC_FLAG_LOW_DELAY;
	this->codec_context->flags2 |= AV_CODEC_FLAG2_FAST;
	// this->codec_context->flags2 |= AV_CODEC_FLAG2_CHUNKS;
	this->codec_context->thread_type = FF_THREAD_SLICE;
	this->codec_context->thread_count = 4;

	if(avcodec_open2(this->codec_context, this->codec, nullptr) < 0)
	{
		avcodec_free_context(&this->codec_context);
		throw Exception("Failed to open codec context");
	}
	return true;
}

bool IO::InitOpenGl()
{
	CHIAKI_LOGV(this->log, "loading OpenGL");

	if(!InitOpenGlShader())
		return false;

	if(!InitOpenGlTextures())
		return false;

	return true;
}

bool IO::InitOpenGlTextures()
{
	CHIAKI_LOGV(this->log, "loading OpenGL textrures");

	D(glGenTextures(PLANES_COUNT, this->tex));
	D(glGenBuffers(PLANES_COUNT, this->pbo));
	uint8_t uv_default[] = { 0x7f, 0x7f };
	for(int i = 0; i < PLANES_COUNT; i++)
	{
		D(glBindTexture(GL_TEXTURE_2D, this->tex[i]));
		D(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		D(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		D(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
		D(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
		D(glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, i > 0 ? uv_default : nullptr));
	}

	D(glUseProgram(this->prog));
	// bind only as many planes as we need
	const char *plane_names[] = { "plane1", "plane2", "plane3" };
	for(int i = 0; i < PLANES_COUNT; i++)
		D(glUniform1i(glGetUniformLocation(this->prog, plane_names[i]), i));

	D(glGenVertexArrays(1, &this->vao));
	D(glBindVertexArray(this->vao));

	D(glGenBuffers(1, &this->vbo));
	D(glBindBuffer(GL_ARRAY_BUFFER, this->vbo));
	D(glBufferData(GL_ARRAY_BUFFER, sizeof(vert_pos), vert_pos, GL_STATIC_DRAW));

	D(glBindBuffer(GL_ARRAY_BUFFER, this->vbo));
	D(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr));
	D(glEnableVertexAttribArray(0));

	D(glCullFace(GL_BACK));
	D(glEnable(GL_CULL_FACE));
	D(glClearColor(0.5, 0.5, 0.5, 1.0));
	return true;
}

GLuint IO::CreateAndCompileShader(GLenum type, const char *source)
{
	GLint success;
	GLchar msg[512];

	GLuint handle;
	D(handle = glCreateShader(type));
	if(!handle)
	{
		CHIAKI_LOGE(this->log, "%u: cannot create shader", type);
		DP(this->prog);
	}

	D(glShaderSource(handle, 1, &source, nullptr));
	D(glCompileShader(handle));
	D(glGetShaderiv(handle, GL_COMPILE_STATUS, &success));

	if(!success)
	{
		D(glGetShaderInfoLog(handle, sizeof(msg), nullptr, msg));
		CHIAKI_LOGE(this->log, "%u: %s\n", type, msg);
		D(glDeleteShader(handle));
	}

	return handle;
}

bool IO::InitOpenGlShader()
{
	CHIAKI_LOGV(this->log, "loading OpenGl Shaders");

	D(this->vert = CreateAndCompileShader(GL_VERTEX_SHADER, shader_vert_glsl));
	D(this->frag = CreateAndCompileShader(GL_FRAGMENT_SHADER, yuv420p_shader_frag_glsl));

	D(this->prog = glCreateProgram());

	D(glAttachShader(this->prog, this->vert));
	D(glAttachShader(this->prog, this->frag));
	D(glBindAttribLocation(this->prog, 0, "pos_attr"));
	D(glLinkProgram(this->prog));

	GLint success;
	D(glGetProgramiv(this->prog, GL_LINK_STATUS, &success));
	if(!success)
	{
		char buf[512];
		glGetProgramInfoLog(this->prog, sizeof(buf), nullptr, buf);
		CHIAKI_LOGE(this->log, "OpenGL link error: %s", buf);
		return false;
	}

	D(glDeleteShader(this->vert));
	D(glDeleteShader(this->frag));

	return true;
}

inline void IO::SetOpenGlYUVPixels(AVFrame *frame)
{
	D(glUseProgram(this->prog));

	int planes[][3] = {
		// { width_divide, height_divider, data_per_pixel }
		{1, 1, 1}, // Y
		{2, 2, 1}, // U
		{2, 2, 1}  // V
	};

	this->mtx.lock();
	for(int i = 0; i < PLANES_COUNT; i++)
	{
		int width = frame->width / planes[i][0];
		int height = frame->height / planes[i][1];
		int size = width * height * planes[i][2];
		uint8_t *buf;

		D(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->pbo[i]));
		D(glBufferData(GL_PIXEL_UNPACK_BUFFER, size, nullptr, GL_STREAM_DRAW));
		D(buf = reinterpret_cast<uint8_t *>(glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT)));
		if(!buf)
		{
			GLint data;
			D(glGetBufferParameteriv(GL_PIXEL_UNPACK_BUFFER, GL_BUFFER_SIZE, &data));
			CHIAKI_LOGE(this->log, "AVOpenGLFrame failed to map PBO");
			CHIAKI_LOGE(this->log, "Info buf == %p. size %d frame %d * %d, divs %d, %d, pbo %d GL_BUFFER_SIZE %x",
				buf, size, frame->width, frame->height, planes[i][0], planes[i][1], pbo[i], data);
			continue;
		}

		if(frame->linesize[i] == width)
		{
			// Y
			memcpy(buf, frame->data[i], size);
		}
		else
		{
			// UV
			for(int l = 0; l < height; l++)
				memcpy(buf + width * l * planes[i][2],
					frame->data[i] + frame->linesize[i] * l,
					width * planes[i][2]);
		}
		D(glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER));
		D(glBindTexture(GL_TEXTURE_2D, tex[i]));
		D(glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr));
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}
	this->mtx.unlock();
	glFinish();
}

inline void IO::OpenGlDraw()
{
	glClear(GL_COLOR_BUFFER_BIT);

	// send to OpenGl
	SetOpenGlYUVPixels(this->frame);

	//avcodec_flush_buffers(this->codec_context);
	D(glBindVertexArray(this->vao));

	for(int i = 0; i < PLANES_COUNT; i++)
	{
		D(glActiveTexture(GL_TEXTURE0 + i));
		D(glBindTexture(GL_TEXTURE_2D, this->tex[i]));
	}

	D(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
	D(glBindVertexArray(0));
	D(glFinish());
}

bool IO::InitJoystick()
{
	// https://github.com/switchbrew/switch-examples/blob/master/graphics/sdl2/sdl2-simple/source/main.cpp#L57
	// open CONTROLLER_PLAYER_1 and CONTROLLER_PLAYER_2
	// when railed, both joycons are mapped to joystick #0,
	// else joycons are individually mapped to joystick #0, joystick #1, ...
	for(int i = 0; i < SDL_JOYSTICK_COUNT; i++)
	{
		this->sdl_joystick_ptr[i] = SDL_JoystickOpen(i);
		if(sdl_joystick_ptr[i] == nullptr)
		{
			CHIAKI_LOGE(this->log, "SDL_JoystickOpen: %s\n", SDL_GetError());
			return false;
		}
	}
	return true;
}

bool IO::FreeJoystick()
{
	for(int i = 0; i < SDL_JOYSTICK_COUNT; i++)
	{
		if(SDL_JoystickGetAttached(sdl_joystick_ptr[i]))
			SDL_JoystickClose(sdl_joystick_ptr[i]);
	}
	return true;
}

bool IO::MainLoop(ChiakiControllerState *state)
{
	D(glUseProgram(this->prog));

	// handle SDL events
	while(SDL_PollEvent(&this->sdl_event))
	{
		this->ReadGameKeys(&this->sdl_event, state);
		switch(this->sdl_event.type)
		{
			case SDL_QUIT:
				return false;
		}
	}

	ReadGameTouchScreen(state);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	OpenGlDraw();

	return !this->quit;
}
