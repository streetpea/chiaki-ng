// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <avopenglwidget.h>
#include <avopenglframeuploader.h>
#include <streamsession.h>

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLDebugLogger>
#include <QThread>
#include <QTimer>

#define MOUSE_TIMEOUT_MS 1000

//#define DEBUG_OPENGL

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

static const char *nv12_shader_frag_glsl = R"glsl(
#version 150 core

uniform sampler2D plane1; // Y
uniform sampler2D plane2; // interlaced UV

in vec2 uv_var;

out vec4 out_color;

void main()
{
	vec3 yuv = vec3(
		(texture(plane1, uv_var).r - (16.0 / 255.0)) / ((235.0 - 16.0) / 255.0),
		(texture(plane2, uv_var).r - (16.0 / 255.0)) / ((240.0 - 16.0) / 255.0) - 0.5,
		(texture(plane2, uv_var).g - (16.0 / 255.0)) / ((240.0 - 16.0) / 255.0) - 0.5
	);
	vec3 rgb = mat3(
		1.0,		1.0,		1.0,
		0.0,		-0.21482,	2.12798,
		1.28033,	-0.38059,	0.0) * yuv;
	out_color = vec4(rgb, 1.0);
}
)glsl";

ConversionConfig conversion_configs[] = {
	{
		AV_PIX_FMT_YUV420P,
		shader_vert_glsl,
		yuv420p_shader_frag_glsl,
		3,
		{
			{ 1, 1, 1, GL_R8, GL_RED },
			{ 2, 2, 1, GL_R8, GL_RED },
			{ 2, 2, 1, GL_R8, GL_RED }
		}
	},
	{
		AV_PIX_FMT_NV12,
		shader_vert_glsl,
		nv12_shader_frag_glsl,
		2,
		{
			{ 1, 1, 1, GL_R8, GL_RED },
			{ 2, 2, 2, GL_RG8, GL_RG }
		}
	}
};

static const float vert_pos[] = {
	0.0f, 0.0f,
	0.0f, 1.0f,
	1.0f, 0.0f,
	1.0f, 1.0f
};


QSurfaceFormat AVOpenGLWidget::CreateSurfaceFormat()
{
	QSurfaceFormat format;
	format.setDepthBufferSize(0);
	format.setStencilBufferSize(0);
	format.setVersion(3, 2);
	format.setProfile(QSurfaceFormat::CoreProfile);
#ifdef DEBUG_OPENGL
	format.setOption(QSurfaceFormat::DebugContext, true);
#endif
	return format;
}

AVOpenGLWidget::AVOpenGLWidget(StreamSession *session, QWidget *parent)
	: QOpenGLWidget(parent),
	session(session)
{
	enum AVPixelFormat pixel_format = chiaki_ffmpeg_decoder_get_pixel_format(session->GetFfmpegDecoder());
	conversion_config = nullptr;
	for(auto &cc: conversion_configs)
	{
		if(pixel_format == cc.pixel_format)
		{
			conversion_config = &cc;
			break;
		}
	}

	if(!conversion_config)
		throw Exception("No matching video conversion config can be found");

	setFormat(CreateSurfaceFormat());

	frame_uploader_context = nullptr;
	frame_uploader = nullptr;
	frame_uploader_thread = nullptr;
	frame_fg = 0;

	setMouseTracking(true);
	mouse_timer = new QTimer(this);
	connect(mouse_timer, &QTimer::timeout, this, &AVOpenGLWidget::HideMouse);
	ResetMouseTimeout();
}

AVOpenGLWidget::~AVOpenGLWidget()
{
	if(frame_uploader_thread)
	{
		frame_uploader_thread->quit();
		frame_uploader_thread->wait();
		delete frame_uploader_thread;
	}
	delete frame_uploader;
	delete frame_uploader_context;
	delete frame_uploader_surface;
}

void AVOpenGLWidget::mouseMoveEvent(QMouseEvent *event)
{
	QOpenGLWidget::mouseMoveEvent(event);
	ResetMouseTimeout();
}

void AVOpenGLWidget::ResetMouseTimeout()
{
	unsetCursor();
	mouse_timer->start(MOUSE_TIMEOUT_MS);
}

void AVOpenGLWidget::HideMouse()
{
	setCursor(Qt::BlankCursor);
}

void AVOpenGLWidget::SwapFrames()
{
	QMutexLocker lock(&frames_mutex);
	frame_fg = 1 - frame_fg;
	QMetaObject::invokeMethod(this, "update");
}

bool AVOpenGLFrame::Update(AVFrame *frame, ChiakiLog *log)
{
	auto f = QOpenGLContext::currentContext()->extraFunctions();

	if(frame->format != conversion_config->pixel_format)
	{
		CHIAKI_LOGE(log, "AVOpenGLFrame got AVFrame with invalid format");
		return false;
	}

	width = frame->width;
	height = frame->height;

	for(int i=0; i<conversion_config->planes; i++)
	{
		int width = frame->width / conversion_config->plane_configs[i].width_divider;
		int height = frame->height / conversion_config->plane_configs[i].height_divider;
		int size = width * height * conversion_config->plane_configs[i].data_per_pixel;

		f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo[i]);
		f->glBufferData(GL_PIXEL_UNPACK_BUFFER, size, nullptr, GL_STREAM_DRAW);

		auto buf = reinterpret_cast<uint8_t *>(f->glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT));
		if(!buf)
		{
			CHIAKI_LOGE(log, "AVOpenGLFrame failed to map PBO");
			continue;
		}

		if(frame->linesize[i] == width)
			memcpy(buf, frame->data[i], size);
		else
		{
			for(int l=0; l<height; l++)
				memcpy(buf + width * l * conversion_config->plane_configs[i].data_per_pixel, frame->data[i] + frame->linesize[i] * l, width * conversion_config->plane_configs[i].data_per_pixel);
		}

		f->glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

		f->glBindTexture(GL_TEXTURE_2D, tex[i]);
		f->glTexImage2D(GL_TEXTURE_2D, 0, conversion_config->plane_configs[i].internal_format, width, height, 0, conversion_config->plane_configs[i].format, GL_UNSIGNED_BYTE, nullptr);
	}

	f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

	f->glFinish();

	return true;
}

void AVOpenGLWidget::initializeGL()
{
	auto f = QOpenGLContext::currentContext()->extraFunctions();

	const char *gl_version = (const char *)f->glGetString(GL_VERSION);
	CHIAKI_LOGI(session->GetChiakiLog(), "OpenGL initialized with version \"%s\"", gl_version ? gl_version : "(null)");

#ifdef DEBUG_OPENGL
	auto logger = new QOpenGLDebugLogger(this);
	logger->initialize();
	connect(logger, &QOpenGLDebugLogger::messageLogged, this, [](const QOpenGLDebugMessage &msg) {
		qDebug() << msg;
	});
	logger->startLogging();
#endif

	auto CheckShaderCompiled = [&](GLuint shader) {
		GLint compiled = 0;
		f->glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if(compiled == GL_TRUE)
			return;
		GLint info_log_size = 0;
		f->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_size);
		QVector<GLchar> info_log(info_log_size);
		f->glGetShaderInfoLog(shader, info_log_size, &info_log_size, info_log.data());
		f->glDeleteShader(shader);
		CHIAKI_LOGE(session->GetChiakiLog(), "Failed to Compile Shader:\n%s", info_log.data());
	};

	GLuint shader_vert = f->glCreateShader(GL_VERTEX_SHADER);
	f->glShaderSource(shader_vert, 1, &conversion_config->shader_vert_glsl, nullptr);
	f->glCompileShader(shader_vert);
	CheckShaderCompiled(shader_vert);

	GLuint shader_frag = f->glCreateShader(GL_FRAGMENT_SHADER);
	f->glShaderSource(shader_frag, 1, &conversion_config->shader_frag_glsl, nullptr);
	f->glCompileShader(shader_frag);
	CheckShaderCompiled(shader_frag);

	program = f->glCreateProgram();
	f->glAttachShader(program, shader_vert);
	f->glAttachShader(program, shader_frag);
	f->glBindAttribLocation(program, 0, "pos_attr");
	f->glLinkProgram(program);

	GLint linked = 0;
	f->glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if(linked != GL_TRUE)
	{
		GLint info_log_size = 0;
		f->glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_size);
		QVector<GLchar> info_log(info_log_size);
		f->glGetProgramInfoLog(program, info_log_size, &info_log_size, info_log.data());
		f->glDeleteProgram(program);
		CHIAKI_LOGE(session->GetChiakiLog(), "Failed to Link Shader Program:\n%s", info_log.data());
		return;
	}

	for(int i=0; i<2; i++)
	{
		frames[i].conversion_config = conversion_config;
		f->glGenTextures(conversion_config->planes, frames[i].tex);
		f->glGenBuffers(conversion_config->planes, frames[i].pbo);
		uint8_t uv_default[] = {0x7f, 0x7f};
		for(int j=0; j<conversion_config->planes; j++)
		{
			f->glBindTexture(GL_TEXTURE_2D, frames[i].tex[j]);
			f->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			f->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			f->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			f->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			f->glTexImage2D(GL_TEXTURE_2D, 0, conversion_config->plane_configs[j].internal_format, 1, 1, 0, conversion_config->plane_configs[j].format, GL_UNSIGNED_BYTE, j > 0 ? uv_default : nullptr);
		}
		frames[i].width = 0;
		frames[i].height = 0;
	}

	f->glUseProgram(program);

	// bind only as many planes as we need
	const char *plane_names[] = {"plane1", "plane2", "plane3"};
	for(int i=0; i<sizeof(plane_names)/sizeof(char *); i++)
	{
		f->glUniform1i(f->glGetUniformLocation(program, plane_names[i]), i);
	}

	f->glGenVertexArrays(1, &vao);
	f->glBindVertexArray(vao);

	f->glGenBuffers(1, &vbo);
	f->glBindBuffer(GL_ARRAY_BUFFER, vbo);
	f->glBufferData(GL_ARRAY_BUFFER, sizeof(vert_pos), vert_pos, GL_STATIC_DRAW);

	f->glBindBuffer(GL_ARRAY_BUFFER, vbo);
	f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
	f->glEnableVertexAttribArray(0);

	f->glCullFace(GL_BACK);
	f->glEnable(GL_CULL_FACE);
	f->glClearColor(0.0, 0.0, 0.0, 1.0);

	frame_uploader_context = new QOpenGLContext(nullptr);
	frame_uploader_context->setFormat(context()->format());
	frame_uploader_context->setShareContext(context());
	if(!frame_uploader_context->create())
	{
		CHIAKI_LOGE(session->GetChiakiLog(), "Failed to create upload OpenGL context");
		return;
	}

	frame_uploader_surface = new QOffscreenSurface();
	frame_uploader_surface->setFormat(context()->format());
	frame_uploader_surface->create();
	frame_uploader = new AVOpenGLFrameUploader(session, this, frame_uploader_context, frame_uploader_surface);
	frame_fg = 0;

	frame_uploader_thread = new QThread(this);
	frame_uploader_thread->setObjectName("Frame Uploader");
	frame_uploader_context->moveToThread(frame_uploader_thread);
	frame_uploader->moveToThread(frame_uploader_thread);
	frame_uploader_thread->start();
}

void AVOpenGLWidget::paintGL()
{
	auto f = QOpenGLContext::currentContext()->extraFunctions();

	f->glClear(GL_COLOR_BUFFER_BIT);

	int widget_width = (int)(width() * devicePixelRatioF());
	int widget_height = (int)(height() * devicePixelRatioF());

	QMutexLocker lock(&frames_mutex);
	AVOpenGLFrame *frame = &frames[frame_fg];

	GLsizei vp_width, vp_height;
	if(!frame->width || !frame->height)
	{
		vp_width = widget_width;
		vp_height = widget_height;
	}
	else
	{
		float aspect = (float)frame->width / (float)frame->height;
		if(aspect < (float)widget_width / (float)widget_height)
		{
			vp_height = widget_height;
			vp_width = (GLsizei)(vp_height * aspect);
		}
		else
		{
			vp_width = widget_width;
			vp_height = (GLsizei)(vp_width / aspect);
		}
	}

	f->glViewport((widget_width - vp_width) / 2, (widget_height - vp_height) / 2, vp_width, vp_height);

	for(int i=0; i<3; i++)
	{
		f->glActiveTexture(GL_TEXTURE0 + i);
		f->glBindTexture(GL_TEXTURE_2D, frame->tex[i]);
	}

	f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	f->glFinish();
}
