// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <avopenglframeuploader.h>
#include <avopenglwidget.h>
#include <streamsession.h>

#include <QOpenGLContext>
#include <QOpenGLFunctions>

AVOpenGLFrameUploader::AVOpenGLFrameUploader(StreamSession *session, AVOpenGLWidget *widget, QOpenGLContext *context, QSurface *surface)
	: QObject(nullptr),
	session(session),
	widget(widget),
	context(context),
	surface(surface)
{
	connect(session, &StreamSession::FfmpegFrameAvailable, this, &AVOpenGLFrameUploader::UpdateFrameFromDecoder);
}

void AVOpenGLFrameUploader::UpdateFrameFromDecoder()
{
	ChiakiFfmpegDecoder *decoder = session->GetFfmpegDecoder();
	if(!decoder)
	{
		CHIAKI_LOGE(session->GetChiakiLog(), "Session has no ffmpeg decoder");
		return;
	}

	if(QOpenGLContext::currentContext() != context)
		context->makeCurrent(surface);

	AVFrame *next_frame = chiaki_ffmpeg_decoder_pull_frame(decoder);
	if(!next_frame)
		return;

	bool success = widget->GetBackgroundFrame()->Update(next_frame, decoder->log);
	av_frame_free(&next_frame);

	if(success)
		widget->SwapFrames();
}
