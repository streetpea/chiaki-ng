// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_GUI_H
#define CHIAKI_GUI_H

#include <glad.h>
#include <GLFW/glfw3.h>
#include "nanovg.h"
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

#include <borealis.hpp>

#include <map>
#include <thread>

#include "discoverymanager.h"
#include "host.h"
#include "io.h"
#include "settings.h"

class HostInterface : public brls::List
{
	private:
		IO *io;
		Host *host;
		Settings *settings;
		bool connected = false;

	public:
		HostInterface(Host *host);
		~HostInterface();

		static void Register(Host *host, std::function<void()> success_cb = nullptr);
		void Register();
		void Wakeup(brls::View *view);
		void Connect(brls::View *view);
		void ConnectSession();
		void Disconnect();
		void Stream();
		void CloseStream(ChiakiQuitEvent *quit);
};

class MainApplication
{
	private:
		Settings *settings;
		ChiakiLog *log;
		DiscoveryManager *discoverymanager;
		IO *io;
		brls::TabFrame *rootFrame;
		std::map<Host *, HostInterface *> host_menuitems;

		bool BuildConfigurationMenu(brls::List *, Host *host = nullptr);
		void BuildAddHostConfigurationMenu(brls::List *);

	public:
		MainApplication(DiscoveryManager *discoverymanager);
		~MainApplication();
		bool Load();
};

class PSRemotePlay : public brls::View
{
	private:
		brls::AppletFrame *frame;
		// to display stream on screen
		IO *io;
		// to send gamepad inputs
		Host *host;
		brls::Label *label;
		ChiakiControllerState state = {0};
		// FPS calculation
		// double base_time;
		// int frame_counter = 0;
		// int fps = 0;

	public:
		PSRemotePlay(Host *host);
		~PSRemotePlay();

		void draw(NVGcontext *vg, int x, int y, unsigned width, unsigned height, brls::Style *style, brls::FrameContext *ctx) override;
};

#endif // CHIAKI_GUI_H
