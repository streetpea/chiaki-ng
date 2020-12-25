// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_AVOPENGLFRAMEUPLOADER_H
#define CHIAKI_AVOPENGLFRAMEUPLOADER_H

#include <QObject>
#include <QOpenGLWidget>

#include <chiaki/ffmpegdecoder.h>

class StreamSession;
class AVOpenGLWidget;
class QSurface;

class AVOpenGLFrameUploader: public QObject
{
	Q_OBJECT

	private:
		StreamSession *session;
		AVOpenGLWidget *widget;
		QOpenGLContext *context;
		QSurface *surface;

	private slots:
		void UpdateFrameFromDecoder();

	public:
		AVOpenGLFrameUploader(StreamSession *session, AVOpenGLWidget *widget, QOpenGLContext *context, QSurface *surface);
};

#endif // CHIAKI_AVOPENGLFRAMEUPLOADER_H
