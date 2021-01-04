// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_IO_H
#define CHIAKI_IO_H

#include <SDL2/SDL.h>
#include <cstdint>
#include <functional>

#include <glad.h> // glad library (OpenGL loader)

#include <chiaki/session.h>

/*
https://github.com/devkitPro/switch-glad/blob/master/include/glad/glad.h
https://glad.dav1d.de/#profile=core&language=c&specification=gl&api=gl%3D4.3&extensions=GL_EXT_texture_compression_s3tc&extensions=GL_EXT_texture_filter_anisotropic

Language/Generator: C/C++
Specification: gl
APIs: gl=4.3
Profile: core
Extensions:
GL_EXT_texture_compression_s3tc,
GL_EXT_texture_filter_anisotropic
Loader: False
Local files: False
Omit khrplatform: False
Reproducible: False
*/

#include <mutex>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <chiaki/controller.h>
#include <chiaki/log.h>

#include "exception.h"

#define PLANES_COUNT 3
#define SDL_JOYSTICK_COUNT 2

class IO
{
	protected:
		IO();
		static IO * instance;
	private:
		ChiakiLog *log;
		int video_width;
		int video_height;
		bool quit = false;
		// opengl reader writer
		std::mutex mtx;
		// default nintendo switch res
		int screen_width = 1280;
		int screen_height = 720;
		AVCodec *codec;
		AVCodecContext *codec_context;
		AVFrame *frame;
		SDL_AudioDeviceID sdl_audio_device_id = 0;
		SDL_Event sdl_event;
		SDL_Joystick *sdl_joystick_ptr[SDL_JOYSTICK_COUNT] = {0};
		GLuint vao;
		GLuint vbo;
		GLuint tex[PLANES_COUNT];
		GLuint pbo[PLANES_COUNT];
		GLuint vert;
		GLuint frag;
		GLuint prog;
		bool InitAVCodec();
		bool InitOpenGl();
		bool InitOpenGlTextures();
		bool InitOpenGlShader();
		void OpenGlDraw();
#ifdef DEBUG_OPENGL
		void CheckGLError(const char *func, const char *file, int line);
		void DumpShaderError(GLuint prog, const char *func, const char *file, int line);
		void DumpProgramError(GLuint prog, const char *func, const char *file, int line);
#endif
		GLuint CreateAndCompileShader(GLenum type, const char *source);
		void SetOpenGlYUVPixels(AVFrame *frame);
		bool ReadGameKeys(SDL_Event *event, ChiakiControllerState *state);
		bool ReadGameTouchScreen(ChiakiControllerState *state);

	public:
		// singleton configuration
		IO(const IO&) = delete;
		void operator=(const IO&) = delete;
		static IO * GetInstance();

		~IO();
		void SetMesaConfig();
		bool VideoCB(uint8_t *buf, size_t buf_size);
		void InitAudioCB(unsigned int channels, unsigned int rate);
		void AudioCB(int16_t *buf, size_t samples_count);
		bool InitVideo(int video_width, int video_height, int screen_width, int screen_height);
		bool FreeVideo();
		bool InitJoystick();
		bool FreeJoystick();
		bool MainLoop(ChiakiControllerState *state);
};

#endif //CHIAKI_IO_H
