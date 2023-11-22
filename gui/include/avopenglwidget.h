// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_AVOPENGLWIDGET_H
#define CHIAKI_AVOPENGLWIDGET_H

#include <chiaki/log.h>

#include <QOpenGLWidget>
#include <QMutex>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_drm.h>
}

#define MAX_PANES 3

class StreamSession;
class AVOpenGLFrameUploader;
class QOffscreenSurface;

struct PlaneConfig
{
	unsigned int width_divider;
	unsigned int height_divider;
	unsigned int data_per_pixel;
	GLint internal_format;
	GLenum format;
};

struct ConversionConfig
{
	enum AVPixelFormat pixel_format;
	const char *shader_vert_glsl;
	const char *shader_frag_glsl;
	unsigned int planes;
	struct PlaneConfig plane_configs[MAX_PANES];
};

struct AVOpenGLFrame
{
	GLuint pbo[MAX_PANES];
	GLuint tex[MAX_PANES];
	unsigned int width;
	unsigned int height;
	ConversionConfig *conversion_config;

	bool Update(AVFrame *frame, ChiakiLog *log);
	bool UpdateDrmPrime(AVFrame *frame, ChiakiLog *log);
};

class AVOpenGLWidget: public QOpenGLWidget
{
	Q_OBJECT

	private:
		StreamSession *session;

		GLuint program;
		GLuint vbo;
		GLuint vao;

		AVOpenGLFrame frames[2];
		int frame_fg;
		QMutex frames_mutex;
		QOffscreenSurface *frame_uploader_surface;
		QOpenGLContext *frame_uploader_context;
		AVOpenGLFrameUploader *frame_uploader;
		QThread *frame_uploader_thread;

		QTimer *mouse_timer;

		ConversionConfig *conversion_config;

	public:
		static QSurfaceFormat CreateSurfaceFormat();

		enum ResolutionMode {Normal = 0, Zoom = 1, Stretch = 2};
		explicit AVOpenGLWidget(StreamSession *session, QWidget *parent = nullptr, ResolutionMode resolution_mode = Normal);
		~AVOpenGLWidget() override;

		void SwapFrames();
		AVOpenGLFrame *GetBackgroundFrame()	{ return &frames[1 - frame_fg]; }

	protected:
		ResolutionMode resolution_mode;
		//void mouseMoveEvent(QMouseEvent *event) override;

		void initializeGL() override;
		void paintGL() override;

	private slots:
		//void ResetMouseTimeout();
	public slots:
		void HideMouse();
		void ToggleStretch();
		void ToggleZoom();
};

#endif // CHIAKI_AVOPENGLWIDGET_H
