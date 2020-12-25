// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// chiaki modules
#include <chiaki/discovery.h>
#include <chiaki/log.h>

// discover and wakeup ps4 host
// from local network
#include "discoverymanager.h"
#include "gui.h"
#include "io.h"
#include "settings.h"

#ifdef __SWITCH__
#include <switch.h>
#else
bool appletMainLoop()
{
	return true;
}
#endif

#if __SWITCH__
#define CHIAKI_ENABLE_SWITCH_NXLINK 1
#endif

#ifdef __SWITCH__
// use a custom nintendo switch socket config
// chiaki requiers many threads with udp/tcp sockets
static const SocketInitConfig g_chiakiSocketInitConfig = {
	.bsdsockets_version = 1,

	.tcp_tx_buf_size = 0x8000,
	.tcp_rx_buf_size = 0x10000,
	.tcp_tx_buf_max_size = 0x40000,
	.tcp_rx_buf_max_size = 0x40000,

	.udp_tx_buf_size = 0x40000,
	.udp_rx_buf_size = 0x40000,

	.sb_efficiency = 8,

	.num_bsd_sessions = 16,
	.bsd_service_type = BsdServiceType_User,
};
#endif // __SWITCH__

#ifdef CHIAKI_ENABLE_SWITCH_NXLINK
static int s_nxlinkSock = -1;

static void initNxLink()
{
	// use chiaki socket config initialization
	if(R_FAILED(socketInitialize(&g_chiakiSocketInitConfig)))
		return;

	s_nxlinkSock = nxlinkStdio();
	if(s_nxlinkSock >= 0)
		printf("initNxLink");
	else
		socketExit();
}

static void deinitNxLink()
{
	if(s_nxlinkSock >= 0)
	{
		close(s_nxlinkSock);
		s_nxlinkSock = -1;
	}
}
#endif // CHIAKI_ENABLE_SWITCH_NXLINK

#ifdef __SWITCH__
extern "C" void userAppInit()
{
#ifdef CHIAKI_ENABLE_SWITCH_NXLINK
	initNxLink();
#endif
	// to load gui resources
	romfsInit();
	plInitialize(PlServiceType_User);
	// load socket custom config
	socketInitialize(&g_chiakiSocketInitConfig);
	setsysInitialize();
}

extern "C" void userAppExit()
{
#ifdef CHIAKI_ENABLE_SWITCH_NXLINK
	deinitNxLink();
#endif // CHIAKI_ENABLE_SWITCH_NXLINK
	socketExit();
	/* Cleanup tesla required services. */
	hidsysExit();
	pmdmntExit();
	plExit();

	/* Cleanup default services. */
	fsExit();
	hidExit();
	appletExit();
	setsysExit();
	smExit();
}
#endif // __SWITCH__

int main(int argc, char * argv[])
{
	// load chiaki lib
	Settings * settings = Settings::GetInstance();
	ChiakiLog * log = settings->GetLogger();

	CHIAKI_LOGI(log, "Loading chaki lib");

	ChiakiErrorCode err = chiaki_lib_init();
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "Chiaki lib init failed: %s\n", chiaki_error_string(err));
		return 1;
	}

	CHIAKI_LOGI(log, "Loading SDL audio / joystick");
	if(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_JOYSTICK))
	{
		CHIAKI_LOGE(log, "SDL initialization failed: %s", SDL_GetError());
		return 1;
	}

	// build sdl OpenGl and AV decoders graphical interface
	IO io = IO(log); // open Input Output class
	DiscoveryManager discoverymanager = DiscoveryManager();
	MainApplication app = MainApplication(&discoverymanager, &io);
	app.Load();

	CHIAKI_LOGI(log, "Quit applet");
	SDL_Quit();
	return 0;
}
