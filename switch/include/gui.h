// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_GUI_H
#define CHIAKI_GUI_H

#include <glad.h>
#include <GLFW/glfw3.h>
#include "nanovg.h"
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

#include <borealis.hpp>

#include <thread>
#include <map>

#include "host.h"
#include "settings.h"
#include "discoverymanager.h"
#include "io.h"

class HostInterface
{
	private:
		brls::TabFrame * root;
		IO * io;
		Host * host;
		Settings * settings;
		brls::List * hostList;
		bool connected = false;
	public:
		HostInterface(brls::List * hostList, IO * io, Host * host, Settings * settings);
		~HostInterface();

		static void Register(IO * io, Host * host,
			Settings * settings, std::function<void()> success_cb = nullptr);
		void Register();
		void Wakeup(brls::View * view);
		void Connect(brls::View * view);
		void ConnectSession();
		void Disconnect();
		bool Stream();
		bool CloseStream(ChiakiQuitEvent * quit);
};

class MainApplication
{
	private:
		ChiakiLog * log;
		std::map<std::string, Host> * hosts;
		Settings * settings;
		DiscoveryManager * discoverymanager;
		IO * io;
		brls::TabFrame * rootFrame;
		std::map<Host *, brls::List *> host_menuitems;
		// add_host local settings
		std::string remote_display_name = "";
		std::string remote_addr = "";
		ChiakiTarget remote_ps_version = CHIAKI_TARGET_PS5_1;

		bool BuildConfigurationMenu(brls::List *, Host * host = nullptr);
		void BuildAddHostConfigurationMenu(brls::List *);
	public:
		MainApplication(std::map<std::string, Host> * hosts,
				Settings * settings, DiscoveryManager * discoverymanager,
				IO * io, ChiakiLog * log);

		~MainApplication();
		bool Load();
};

class PSRemotePlay : public brls::View
{
	private:
		brls::AppletFrame * frame;
		// to display stream on screen
		IO * io;
		// to send gamepad inputs
		Host * host;
		brls::Label * label;
		ChiakiControllerState state = { 0 };
		// FPS calculation
		// double base_time;
		// int frame_counter = 0;
		// int fps = 0;

	public:
		PSRemotePlay(IO * io, Host * host);
		~PSRemotePlay();

		void draw(NVGcontext * vg, int x, int y, unsigned width, unsigned height, brls::Style * style, brls::FrameContext * ctx) override;
};

#endif // CHIAKI_GUI_H

