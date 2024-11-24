// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_GUI_H
#define CHIAKI_GUI_H

#include <glad.h>
#include <GLFW/glfw3.h>
#include "nanovg.h"
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

#include <map>
#include <thread>
#include <fmt/format.h>

#include <borealis.hpp>
#include "discoverymanager.h"
#include "host.h"
#include "io.h"
#include "settings.h"
#include "switch.h"
#include "views/enter_pin_view.h"
#include "views/ps_remote_play.h"

class HostInterface : public brls::List
{
	private:
		IO *io;
		Host *host;
		Settings *settings;
		ChiakiLog *log = nullptr;
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
		void EnterPin(bool isError);
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

#endif // CHIAKI_GUI_H
