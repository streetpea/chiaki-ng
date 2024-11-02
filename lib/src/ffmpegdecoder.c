
#include <chiaki/ffmpegdecoder.h>

#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>

static enum AVCodecID chiaki_codec_av_codec_id(ChiakiCodec codec)
{
	switch(codec)
	{
		case CHIAKI_CODEC_H265:
		case CHIAKI_CODEC_H265_HDR:
			return AV_CODEC_ID_H265;
		default:
			return AV_CODEC_ID_H264;
	}
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_ffmpeg_decoder_init(ChiakiFfmpegDecoder *decoder, ChiakiLog *log,
		ChiakiCodec codec, const char *hw_decoder_name, AVBufferRef *hw_device_ctx,
		ChiakiFfmpegFrameAvailable frame_available_cb, void *frame_available_cb_user)
{
	ChiakiErrorCode err = chiaki_mutex_init(&decoder->mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	chiaki_mutex_lock(&decoder->mutex);
	decoder->log = log;
	decoder->frame_available_cb = frame_available_cb;
	decoder->frame_available_cb_user = frame_available_cb_user;
	decoder->hdr_enabled = codec == CHIAKI_CODEC_H265_HDR;
	decoder->frames_lost = 0;
	decoder->frame_recovered = false;

	decoder->hw_device_ctx = hw_device_ctx ? av_buffer_ref(hw_device_ctx) : NULL;
	decoder->hw_pix_fmt = AV_PIX_FMT_NONE;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 10, 100)
	avcodec_register_all();
#endif
	enum AVCodecID av_codec = chiaki_codec_av_codec_id(codec);
	decoder->av_codec = avcodec_find_decoder(av_codec);
	if(!decoder->av_codec)
	{
		CHIAKI_LOGE(log, "%s Codec not available", chiaki_codec_name(codec));
		goto error_mutex;
	}

	decoder->codec_context = avcodec_alloc_context3(decoder->av_codec);
	if(!decoder->codec_context)
	{
		CHIAKI_LOGE(log, "Failed to alloc codec context");
		goto error_mutex;
	}

	if(hw_decoder_name)
	{
		CHIAKI_LOGI(log, "Trying to use hardware decoder \"%s\"", hw_decoder_name);
		enum AVHWDeviceType type = av_hwdevice_find_type_by_name(hw_decoder_name);
		if(type == AV_HWDEVICE_TYPE_NONE)
		{
			CHIAKI_LOGE(log, "Hardware decoder \"%s\" not found", hw_decoder_name);
			goto error_codec_context;
		}

		for(int i = 0;; i++)
		{
			const AVCodecHWConfig *config = avcodec_get_hw_config(decoder->av_codec, i);
			if(!config)
			{
				CHIAKI_LOGE(log, "avcodec_get_hw_config failed");
				goto error_codec_context;
			}
			if(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == type)
			{
				decoder->hw_pix_fmt = config->pix_fmt;
				break;
			}
		}

		if(!decoder->hw_device_ctx && av_hwdevice_ctx_create(&decoder->hw_device_ctx, type, NULL, NULL, 0) < 0)
		{
			CHIAKI_LOGE(log, "Failed to create hwdevice context");
			goto error_codec_context;
		}
		decoder->codec_context->hw_device_ctx = av_buffer_ref(decoder->hw_device_ctx);
		CHIAKI_LOGI(log, "Using hardware decoder \"%s\" with pix_fmt=%s", hw_decoder_name, av_get_pix_fmt_name(decoder->hw_pix_fmt));
	}

	if(avcodec_open2(decoder->codec_context, decoder->av_codec, NULL) < 0)
	{
		CHIAKI_LOGE(log, "Failed to open codec context");
		goto error_codec_context;
	}
	chiaki_mutex_unlock(&decoder->mutex);
	return CHIAKI_ERR_SUCCESS;
error_codec_context:
	if(decoder->hw_device_ctx)
		av_buffer_unref(&decoder->hw_device_ctx);
	avcodec_free_context(&decoder->codec_context);
error_mutex:
	chiaki_mutex_unlock(&decoder->mutex);
	chiaki_mutex_fini(&decoder->mutex);
	return CHIAKI_ERR_UNKNOWN;
}

CHIAKI_EXPORT void chiaki_ffmpeg_decoder_fini(ChiakiFfmpegDecoder *decoder)
{
	chiaki_mutex_lock(&decoder->mutex);
	avcodec_free_context(&decoder->codec_context);
	if(decoder->hw_device_ctx)
		av_buffer_unref(&decoder->hw_device_ctx);
	chiaki_mutex_unlock(&decoder->mutex);
	chiaki_mutex_fini(&decoder->mutex);
}

CHIAKI_EXPORT bool chiaki_ffmpeg_decoder_video_sample_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user)
{
	ChiakiFfmpegDecoder *decoder = user;

	chiaki_mutex_lock(&decoder->mutex);
	decoder->frames_lost += frames_lost;
	decoder->frame_recovered = frame_recovered;
	AVPacket *packet = av_packet_alloc();
	packet->data = buf;
	packet->size = buf_size;
	int r;
send_packet:
	r = avcodec_send_packet(decoder->codec_context, packet);
	if(r != 0)
	{
		if(r == AVERROR(EAGAIN))
		{
			CHIAKI_LOGE(decoder->log, "AVCodec internal buffer is full removing frames before pushing");
			AVFrame *frame = av_frame_alloc();
			if(!frame)
			{
				CHIAKI_LOGE(decoder->log, "Failed to alloc AVFrame");
				goto hell;
			}
			r = avcodec_receive_frame(decoder->codec_context, frame);
			av_frame_free(&frame);
			if(r != 0)
			{
				CHIAKI_LOGE(decoder->log, "Failed to pull frame");
				goto hell;
			}
			goto send_packet;
		}
		else
		{
			char errbuf[128];
			av_make_error_string(errbuf, sizeof(errbuf), r);
			CHIAKI_LOGE(decoder->log, "Failed to push frame: %s", errbuf);
			goto hell;
		}
	}
	av_packet_free(&packet);
	chiaki_mutex_unlock(&decoder->mutex);

	decoder->frame_available_cb(decoder, decoder->frame_available_cb_user);
	return true;
hell:
	av_packet_free(&packet);
	chiaki_mutex_unlock(&decoder->mutex);
	return false;
}

CHIAKI_EXPORT AVFrame *chiaki_ffmpeg_decoder_pull_frame(ChiakiFfmpegDecoder *decoder, int32_t *frames_lost)
{
	chiaki_mutex_lock(&decoder->mutex);
	// always try to pull as much as possible and return only the very last frame
	AVFrame *frame_last = NULL;
	AVFrame *frame = NULL;
	while(true)
	{
		AVFrame *next_frame;
		if(frame_last)
		{
			av_frame_unref(frame_last);
			next_frame = frame_last;
		}
		else
		{
			next_frame = av_frame_alloc();
			if(!next_frame)
				break;
		}
		frame_last = frame;
		frame = next_frame;
		int r = avcodec_receive_frame(decoder->codec_context, frame);
		if(r)
		{
			if(r != AVERROR(EAGAIN))
				CHIAKI_LOGE(decoder->log, "Decoding with FFMPEG failed");
			av_frame_free(&frame);
			frame = frame_last;
			break;
		}
	}
	*frames_lost = decoder->frames_lost;
	if(frame && decoder->frame_recovered)
	{
		decoder->frame_recovered = false;
		frame->decode_error_flags |= 1;
	}
	decoder->frames_lost = 0;
	chiaki_mutex_unlock(&decoder->mutex);

	return frame;
}

CHIAKI_EXPORT enum AVPixelFormat chiaki_ffmpeg_decoder_get_pixel_format(ChiakiFfmpegDecoder *decoder)
{
	if (decoder->hw_device_ctx) {
		return decoder->hdr_enabled
			? AV_PIX_FMT_P010LE
			: AV_PIX_FMT_NV12;
	} else {
		return decoder->hdr_enabled
			? AV_PIX_FMT_YUV420P10LE
			: AV_PIX_FMT_YUV420P;
	}
}

