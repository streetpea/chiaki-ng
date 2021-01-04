
#include <chiaki/pidecoder.h>

#include <bcm_host.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


CHIAKI_EXPORT ChiakiErrorCode chiaki_pi_decoder_init(ChiakiPiDecoder *decoder, ChiakiLog *log)
{
	memset(decoder, 0, sizeof(ChiakiPiDecoder));

	bcm_host_init();

	if(!(decoder->client = ilclient_init()))
	{
		CHIAKI_LOGE(decoder->log, "ilclient_init failed");
		chiaki_pi_decoder_fini(decoder);
		return CHIAKI_ERR_UNKNOWN;
	}

	if(OMX_Init() != OMX_ErrorNone)
	{
		CHIAKI_LOGE(decoder->log, "OMX_Init failed");
		chiaki_pi_decoder_fini(decoder);
		return CHIAKI_ERR_UNKNOWN;
	}

	if(ilclient_create_component(decoder->client, &decoder->video_decode, "video_decode",
			ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS) != 0)
	{
		CHIAKI_LOGE(decoder->log, "ilclient_create_component failed for video_decode");
		chiaki_pi_decoder_fini(decoder);
		return CHIAKI_ERR_UNKNOWN;
	}
	decoder->components[0] = decoder->video_decode;

	if(ilclient_create_component(decoder->client, &decoder->video_render, "video_render", ILCLIENT_DISABLE_ALL_PORTS) != 0)
	{
		CHIAKI_LOGE(decoder->log, "ilclient_create_component failed for video_render");
		chiaki_pi_decoder_fini(decoder);
		return CHIAKI_ERR_UNKNOWN;
	}
	decoder->components[1] = decoder->video_render;

	set_tunnel(decoder->tunnel, decoder->video_decode, 131, decoder->video_render, 90);

	ilclient_change_component_state(decoder->video_decode, OMX_StateIdle);

	OMX_VIDEO_PARAM_PORTFORMATTYPE format;
	memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
	format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
	format.nVersion.nVersion = OMX_VERSION;
	format.nPortIndex = 130;
	format.eCompressionFormat = OMX_VIDEO_CodingAVC;

	OMX_PARAM_DATAUNITTYPE unit;
	memset(&unit, 0, sizeof(OMX_PARAM_DATAUNITTYPE));
	unit.nSize = sizeof(OMX_PARAM_DATAUNITTYPE);
	unit.nVersion.nVersion = OMX_VERSION;
	unit.nPortIndex = 130;
	unit.eUnitType = OMX_DataUnitCodedPicture;
	unit.eEncapsulationType = OMX_DataEncapsulationElementaryStream;

	if(OMX_SetParameter(ILC_GET_HANDLE(decoder->video_decode), OMX_IndexParamVideoPortFormat, &format) != OMX_ErrorNone
		|| OMX_SetParameter(ILC_GET_HANDLE(decoder->video_decode), OMX_IndexParamBrcmDataUnit, &unit) != OMX_ErrorNone)
	{
		CHIAKI_LOGE(decoder->log, "OMX_SetParameter failed for video parameters");
		chiaki_pi_decoder_fini(decoder);
		return CHIAKI_ERR_UNKNOWN;
	}

	OMX_CONFIG_LATENCYTARGETTYPE latencyTarget;
	memset(&latencyTarget, 0, sizeof(OMX_CONFIG_LATENCYTARGETTYPE));
	latencyTarget.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	latencyTarget.nVersion.nVersion = OMX_VERSION;
	latencyTarget.nPortIndex = 90;
	latencyTarget.bEnabled = OMX_TRUE;
	latencyTarget.nFilter = 2;
	latencyTarget.nTarget = 4000;
	latencyTarget.nShift = 3;
	latencyTarget.nSpeedFactor = -135;
	latencyTarget.nInterFactor = 500;
	latencyTarget.nAdjCap = 20;

	if(OMX_SetParameter(ILC_GET_HANDLE(decoder->video_render), OMX_IndexConfigLatencyTarget, &latencyTarget) != OMX_ErrorNone)
	{
		CHIAKI_LOGE(decoder->log, "OMX_SetParameter failed for render parameters");
		chiaki_pi_decoder_fini(decoder);
		return CHIAKI_ERR_UNKNOWN;
	}

	OMX_CONFIG_ROTATIONTYPE rotationType;
	memset(&rotationType, 0, sizeof(OMX_CONFIG_ROTATIONTYPE));
	rotationType.nSize = sizeof(OMX_CONFIG_ROTATIONTYPE);
	rotationType.nVersion.nVersion = OMX_VERSION;
	rotationType.nPortIndex = 90;
	//rotationType.nRotation = 90; // example

	if(OMX_SetParameter(ILC_GET_HANDLE(decoder->video_render), OMX_IndexConfigCommonRotate, &rotationType) != OMX_ErrorNone)
	{
		CHIAKI_LOGE(decoder->log, "OMX_SetParameter failed for rotation");
		chiaki_pi_decoder_fini(decoder);
		return CHIAKI_ERR_UNKNOWN;
	}

	if(ilclient_enable_port_buffers(decoder->video_decode, 130, NULL, NULL, NULL) != 0)
	{
		CHIAKI_LOGE(decoder->log, "ilclient_enable_port_buffers failed");
		chiaki_pi_decoder_fini(decoder);
		return CHIAKI_ERR_UNKNOWN;
	}

	decoder->port_settings_changed = false;
	decoder->first_packet = true;

	ilclient_change_component_state(decoder->video_decode, OMX_StateExecuting);

	CHIAKI_LOGI(decoder->log, "Raspberry Pi Decoder initialized");
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT void chiaki_pi_decoder_fini(ChiakiPiDecoder *decoder)
{
	if(decoder->video_decode)
	{
		OMX_BUFFERHEADERTYPE *buf;
		if((buf = ilclient_get_input_buffer(decoder->video_decode, 130, 1)))
		{
			buf->nFilledLen = 0;
			buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;
			OMX_EmptyThisBuffer(ILC_GET_HANDLE(decoder->video_decode), buf);
		}

		// need to flush the renderer to allow video_decode to disable its input port
		ilclient_flush_tunnels(decoder->tunnel, 0);

		ilclient_disable_port_buffers(decoder->video_decode, 130, NULL, NULL, NULL);

		ilclient_disable_tunnel(decoder->tunnel);
		ilclient_teardown_tunnels(decoder->tunnel);

		ilclient_state_transition(decoder->components, OMX_StateIdle);
		ilclient_state_transition(decoder->components, OMX_StateLoaded);

		ilclient_cleanup_components(decoder->components);
	}

	OMX_Deinit();
	if(decoder->client)
		ilclient_destroy(decoder->client);
}

static bool push_buffer(ChiakiPiDecoder *decoder, uint8_t *buf, size_t buf_size)
{
	while(buf_size)
	{
		OMX_BUFFERHEADERTYPE *omx_buf = ilclient_get_input_buffer(decoder->video_decode, 130, 1);
		if(!omx_buf)
		{
			CHIAKI_LOGE(decoder->log, "ilclient_get_input_buffer failed");
			return false;
		}

		size_t push_size = buf_size;
		if(push_size > omx_buf->nAllocLen)
		{
			CHIAKI_LOGW(decoder->log, "OMX Buffer too small, fragmenting to multiple buffers.");
			push_size = omx_buf->nAllocLen;
		}
		memcpy(omx_buf->pBuffer, buf, push_size);
		buf_size -= push_size;
		buf += push_size;
		omx_buf->nFilledLen = push_size;
		omx_buf->nOffset = 0;
		omx_buf->nFlags = 0;
		if(!buf_size)
			omx_buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
		if(decoder->first_packet)
		{
			omx_buf->nFlags |= OMX_BUFFERFLAG_STARTTIME;
			decoder->first_packet = false;
		}

		if(!decoder->port_settings_changed
			&& ((omx_buf->nFilledLen > 0 && ilclient_remove_event(decoder->video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1) == 0)
				|| (omx_buf->nFilledLen == 0 && ilclient_wait_for_event(decoder->video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1, ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 10000) == 0)))
		{
			decoder->port_settings_changed = true;

			if(ilclient_setup_tunnel(decoder->tunnel, 0, 0) != 0)
			{
				CHIAKI_LOGE(decoder->log, "ilclient_setup_tunnel failed");
				return false;
			}

			ilclient_change_component_state(decoder->video_render, OMX_StateExecuting);
		}

		if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(decoder->video_decode), omx_buf) != OMX_ErrorNone)
		{
			CHIAKI_LOGE(decoder->log, "OMX_EmptyThisBuffer failed");
			return false;
		}
	}
	return true;
}

#define OMX_INIT_STRUCTURE(a) \
  memset(&(a), 0, sizeof(a)); \
  (a).nSize = sizeof(a); \
  (a).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
  (a).nVersion.s.nVersionMinor = OMX_VERSION_MINOR; \
  (a).nVersion.s.nRevision = OMX_VERSION_REVISION; \
  (a).nVersion.s.nStep = OMX_VERSION_STEP

CHIAKI_EXPORT void chiaki_pi_decoder_set_params(ChiakiPiDecoder *decoder, int x, int y, int w, int h, bool visible)
{
	OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
	OMX_INIT_STRUCTURE(configDisplay);
	configDisplay.nPortIndex = 90;
	configDisplay.set        = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_NOASPECT | OMX_DISPLAY_SET_MODE | OMX_DISPLAY_SET_FULLSCREEN | OMX_DISPLAY_SET_PIXEL | OMX_DISPLAY_SET_DEST_RECT | OMX_DISPLAY_SET_ALPHA);
	configDisplay.mode = OMX_DISPLAY_MODE_LETTERBOX;
	configDisplay.fullscreen = OMX_FALSE;
	configDisplay.noaspect   = OMX_FALSE;
	configDisplay.dest_rect.x_offset  = x;
	configDisplay.dest_rect.y_offset  = y;
	configDisplay.dest_rect.width     = w;
	configDisplay.dest_rect.height    = h;
	configDisplay.alpha = visible ? 255 : 0;

	if(OMX_SetParameter(ILC_GET_HANDLE(decoder->video_render), OMX_IndexConfigDisplayRegion, &configDisplay) != OMX_ErrorNone)
		CHIAKI_LOGE(decoder->log, "OMX_SetParameter failed for display params");
}

CHIAKI_EXPORT bool chiaki_pi_decoder_video_sample_cb(uint8_t *buf, size_t buf_size, void *user)
{
	return push_buffer(user, buf, buf_size);
}
