// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/log.h>
#include "gui.h"

#define SCREEN_W 1280
#define SCREEN_H 720

// TODO
using namespace brls::i18n::literals; // for _i18n

#define DIALOG(dialog, r) \
	brls::Dialog* d_##dialog = new brls::Dialog(r); \
	brls::GenericEvent::Callback cb_##dialog = [d_##dialog](brls::View* view) \
	{ \
		d_##dialog->close(); \
	}; \
	d_##dialog->addButton("Ok", cb_##dialog); \
	d_##dialog->setCancelable(false); \
	d_##dialog->open(); \
	brls::Logger::info("Dialog: {0}", r);


HostInterface::HostInterface(brls::List * hostList, IO * io, Host * host, Settings * settings)
	: hostList(hostList), io(io), host(host), settings(settings)
{
	brls::ListItem* connect = new brls::ListItem("Connect");
	connect->getClickEvent()->subscribe(std::bind(&HostInterface::Connect, this, std::placeholders::_1));
	this->hostList->addView(connect);

	brls::ListItem* wakeup = new brls::ListItem("Wakeup");
	wakeup->getClickEvent()->subscribe(std::bind(&HostInterface::Wakeup, this, std::placeholders::_1));
	this->hostList->addView(wakeup);

	// message delimiter
	brls::Label* info = new brls::Label(brls::LabelStyle::REGULAR,
			"Host configuration", true);
	this->hostList->addView(info);

	// push opengl chiaki stream
	// when the host is connected
	this->io->SetEventConnectedCallback(std::bind(&HostInterface::Stream, this));
	this->io->SetEventQuitCallback(std::bind(&HostInterface::CloseStream, this, std::placeholders::_1));
}

HostInterface::~HostInterface()
{
	Disconnect();
}

void HostInterface::Register(IO * io, Host * host, Settings * settings, std::function<void()> success_cb)
{
	// user must provide psn id for registration
	std::string account_id = settings->GetPSNAccountID(host);
	std::string online_id = settings->GetPSNOnlineID(host);
	ChiakiTarget target = host->GetChiakiTarget();

	if(target >= CHIAKI_TARGET_PS4_9 && account_id.length() <= 0)
	{
		// PS4 firmware > 7.0
		DIALOG(upaid, "Undefined PSN Account ID (Please configure a valid psn_account_id)");
		return;
	}
	else if(target < CHIAKI_TARGET_PS4_9 && online_id.length() <= 0)
	{
		// use oline ID for ps4 < 7.0
		DIALOG(upoid, "Undefined PSN Online ID (Please configure a valid psn_online_id)");
		return;
	}

	// add HostConnected function to regist_event_type_finished_success
	auto event_type_finished_success_cb = [settings, success_cb]()
	{
			// save RP keys
			settings->WriteFile();
			if(success_cb != nullptr)
			{
				// FIXME: may raise a connection refused
				// when the connection is triggered
				// just after the register success
				sleep(2);
				success_cb();
			}
			// decrement block input token number
			brls::Application::unblockInputs();
	};
	host->SetRegistEventTypeFinishedSuccess(event_type_finished_success_cb);

	auto event_type_finished_failed_cb = []()
	{
			// unlock user inputs
			brls::Application::unblockInputs();
			brls::Application::notify("Registration failed");
	};
	host->SetRegistEventTypeFinishedFailed(event_type_finished_failed_cb);

	// the host is not registered yet
	brls::Dialog* peprpc = new brls::Dialog("Please enter your PlayStation registration PIN code");
	brls::GenericEvent::Callback cb_peprpc = [host, io, peprpc](brls::View* view)
	{
		bool pin_provided = false;
		char pin_input[9] = {0};
		std::string error_message;

		// use callback to ensure that the message is showed on screen
		// before the the ReadUserKeyboard
		peprpc->close();

		pin_provided = io->ReadUserKeyboard(pin_input, sizeof(pin_input));
		if(pin_provided)
		{
			// prevent users form messing with the gui
			brls::Application::blockInputs();
			int ret = host->Register(pin_input);
			if(ret != HOST_REGISTER_OK)
			{
				switch(ret)
				{
					// account not configured
					case HOST_REGISTER_ERROR_SETTING_PSNACCOUNTID:
						brls::Application::notify("No PSN Account ID provided");
						brls::Application::unblockInputs();
						break;
					case HOST_REGISTER_ERROR_SETTING_PSNONLINEID:
						brls::Application::notify("No PSN Online ID provided");
						brls::Application::unblockInputs();
						break;
				}
			}
		}
	};
	peprpc->addButton("Ok", cb_peprpc);
	peprpc->setCancelable(false);
	peprpc->open();
}

void HostInterface::Register()
{
	// use Connect just after the registration to save user inputs
	HostInterface::Register(this->io, this->host,
		this->settings, std::bind(&HostInterface::ConnectSession, this));
}

void HostInterface::Wakeup(brls::View * view)
{
	if(!this->host->HasRPkey())
	{
		// the host is not registered yet
		DIALOG(prypf, "Please register your PlayStation first");
	}
	else
	{
		int r = host->Wakeup();
		if(r == 0)
		{
			brls::Application::notify("PlayStation Wakeup packet sent");
		}
		else
		{
			brls::Application::notify("PlayStation Wakeup packet failed");
		}
	}
}

void HostInterface::Connect(brls::View * view)
{
	// check that all requirements are met
	if(!this->host->IsDiscovered() && !this->host->HasRPkey())
	{
		// at this point the host must be discovered or registered manually
		// to validate the system_version accuracy
		brls::Application::crash("Undefined PlayStation remote version");
	}

	// ignore state for remote hosts
	if(this->host->IsDiscovered() && this->host->IsReady())
	{
		// host in standby mode
		DIALOG(ptoyp, "Please turn on your PlayStation");
		return;
	}

	if(!this->host->HasRPkey())
	{
		this->Register();
	}
	else
	{
		// the host is already registered
		// start session directly
		ConnectSession();
	}
}

void HostInterface::ConnectSession()
{
	// ignore all user inputs (avoid double connect)
	// user inputs are restored with the CloseStream
	brls::Application::blockInputs();

	// connect host sesssion
	this->host->InitSession(this->io);
	this->host->StartSession();
}

void HostInterface::Disconnect()
{
	if(this->connected)
	{
		brls::Application::popView();
		this->host->StopSession();
		this->connected = false;
	}
	this->host->FiniSession();
}

bool HostInterface::Stream()
{
	this->connected = true;
	// https://github.com/natinusala/borealis/issues/59
	// disable 60 fps limit
	brls::Application::setMaximumFPS(0);

	// show FPS counter
	// brls::Application::setDisplayFramerate(true);

	// push raw opengl stream over borealis
	brls::Application::pushView(new PSRemotePlay(this->io, this->host));
	return true;
}

bool HostInterface::CloseStream(ChiakiQuitEvent * quit)
{
	// session QUIT call back
	brls::Application::unblockInputs();

	// restore 60 fps limit
	brls::Application::setMaximumFPS(60);

	// brls::Application::setDisplayFramerate(false);
	/*
	  DIALOG(sqrs, chiaki_quit_reason_string(quit->reason));
	*/
	brls::Application::notify(chiaki_quit_reason_string(quit->reason));
	Disconnect();
	return false;
}

MainApplication::MainApplication(std::map<std::string, Host> * hosts,
		Settings * settings, DiscoveryManager * discoverymanager,
		IO * io, ChiakiLog * log)
	: hosts(hosts), settings(settings), discoverymanager(discoverymanager),
	io(io), log(log)
{
}

MainApplication::~MainApplication()
{
	this->discoverymanager->SetService(false);
	//this->io->FreeJoystick();
	this->io->FreeVideo();
}

bool MainApplication::Load()
{
	this->discoverymanager->SetService(true);
	// Init the app
	brls::Logger::setLogLevel(brls::LogLevel::DEBUG);

	brls::i18n::loadTranslations();
	if (!brls::Application::init("Chiaki Remote play"))
	{
		brls::Logger::error("Unable to init Borealis application");
		return false;
	}

	// init chiaki gl after borealis
	// let borealis manage the main screen/window

	if(!io->InitVideo(0, 0, SCREEN_W, SCREEN_H))
	{
		brls::Logger::error("Failed to initiate Video");
	}

	brls::Logger::info("Load sdl joysticks");
	if(!io->InitJoystick())
	{
		brls::Logger::error("Faled to initiate Joysticks");
	}

	// Create a view
	this->rootFrame = new brls::TabFrame();
	this->rootFrame->setTitle("Chiaki: Open Source PlayStation Remote Play Client");
	this->rootFrame->setIcon(BOREALIS_ASSET("icon.jpg"));

	brls::List* config = new brls::List();
	brls::List* add_host = new brls::List();

	BuildConfigurationMenu(config);
	BuildAddHostConfigurationMenu(add_host);
	this->rootFrame->addTab("Configuration", config);
	this->rootFrame->addTab("Add Host", add_host);
	// ----------------
	this->rootFrame->addSeparator();

	// Add the root view to the stack
	brls::Application::pushView(this->rootFrame);
	while(brls::Application::mainLoop())
	{
		for(auto it = this->hosts->begin(); it != this->hosts->end(); it++)
		{
			// add host to the gui only if the host is registered or discovered
			if(this->host_menuitems.find(&it->second) == this->host_menuitems.end()
				&& (it->second.HasRPkey() == true || it->second.IsDiscovered() == true))
			{
				brls::List* new_host = new brls::List();
				this->host_menuitems[&it->second] = new_host;
				// create host if udefined
				HostInterface host_menu = HostInterface(new_host, this->io, &it->second, this->settings);
				BuildConfigurationMenu(new_host, &it->second);
				this->rootFrame->addTab(it->second.GetHostName().c_str(), new_host);
			}
		}
	}
	return true;
}

bool MainApplication::BuildConfigurationMenu(brls::List * ls, Host * host)
{
	std::string psn_account_id_string = this->settings->GetPSNAccountID(host);
	brls::ListItem* psn_account_id = new brls::ListItem("PSN Account ID",  "PS5 or PS4 v7.0 and greater (base64 account_id)");
	psn_account_id->setValue(psn_account_id_string.c_str());
	auto psn_account_id_cb = [this, host, psn_account_id](brls::View * view)
	{
		char account_id[CHIAKI_PSN_ACCOUNT_ID_SIZE * 2] = {0};
		bool input = this->io->ReadUserKeyboard(account_id, sizeof(account_id));
		if(input)
		{
			// update gui
			psn_account_id->setValue(account_id);
			// push in setting
			this->settings->SetPSNAccountID(host, account_id);
			// write on disk
			this->settings->WriteFile();
		}
	};
	psn_account_id->getClickEvent()->subscribe(psn_account_id_cb);
	ls->addView(psn_account_id);

	std::string psn_online_id_string = this->settings->GetPSNOnlineID(host);
	brls::ListItem* psn_online_id = new brls::ListItem("PSN Online ID");
	psn_online_id->setValue(psn_online_id_string.c_str());
	auto psn_online_id_cb = [this, host, psn_online_id](brls::View * view)
	{
		char online_id[256] = {0};
		bool input = this->io->ReadUserKeyboard(online_id, sizeof(online_id));
		if(input)
		{
			// update gui
			psn_online_id->setValue(online_id);
			// push in setting
			this->settings->SetPSNOnlineID(host, online_id);
			// write on disk
			this->settings->WriteFile();
		}
	};
	psn_online_id->getClickEvent()->subscribe(psn_online_id_cb);
	ls->addView(psn_online_id);

	int value;
	ChiakiVideoResolutionPreset resolution_preset = this->settings->GetVideoResolution(host);
	switch(resolution_preset)
	{
		case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
			value = 0;
			break;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
			value = 1;
			break;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
			value = 2;
			break;
	}

	brls::SelectListItem* resolution = new brls::SelectListItem(
			"Resolution", { "720p", "540p", "360p" }, value);

	auto resolution_cb = [this, host](int result)
	{
		ChiakiVideoResolutionPreset value = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
		switch(result)
		{
			case 0:
				value = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
				break;
			case 1:
				value = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
				break;
			case 2:
				value = CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
				break;
		}
		this->settings->SetVideoResolution(host, value);
		this->settings->WriteFile();
	};
	resolution->getValueSelectedEvent()->subscribe(resolution_cb);
	ls->addView(resolution);

	ChiakiVideoFPSPreset fps_preset = this->settings->GetVideoFPS(host);
	switch(fps_preset)
	{
		case CHIAKI_VIDEO_FPS_PRESET_60:
			value = 0;
			break;
		case CHIAKI_VIDEO_FPS_PRESET_30:
			value = 1;
			break;
	}

	brls::SelectListItem* fps = new brls::SelectListItem(
			"FPS", { "60", "30"}, value);

	auto fps_cb = [this, host](int result)
	{
		ChiakiVideoFPSPreset value = CHIAKI_VIDEO_FPS_PRESET_60;
		switch(result)
		{
			case 0:
				value = CHIAKI_VIDEO_FPS_PRESET_60;
				break;
			case 1:
				value = CHIAKI_VIDEO_FPS_PRESET_30;
				break;
		}
		this->settings->SetVideoFPS(host, value);
		this->settings->WriteFile();
	};

	fps->getValueSelectedEvent()->subscribe(fps_cb);
	ls->addView(fps);

	if(host != nullptr)
	{
		// message delimiter
		brls::Label* info = new brls::Label(brls::LabelStyle::REGULAR,
				"Host information", true);
		ls->addView(info);

		std::string host_name_string = this->settings->GetHostName(host);
		brls::ListItem* host_name = new brls::ListItem("PS Hostname");
		host_name->setValue(host_name_string.c_str());
		ls->addView(host_name);

		std::string host_addr_string = settings->GetHostAddr(host);
		brls::ListItem* host_addr = new brls::ListItem("PS4 Address");
		host_addr->setValue(host_addr_string.c_str());
		ls->addView(host_addr);

		std::string host_rp_regist_key_string = settings->GetHostRPRegistKey(host);
		brls::ListItem* host_rp_regist_key = new brls::ListItem("RP Register Key");
		host_rp_regist_key->setValue(host_rp_regist_key_string.c_str());
		ls->addView(host_rp_regist_key);

		std::string host_rp_key_string = settings->GetHostRPKey(host);
		brls::ListItem* host_rp_key = new brls::ListItem("RP Key");
		host_rp_key->setValue(host_rp_key_string.c_str());
		ls->addView(host_rp_key);

		std::string host_rp_key_type_string = std::to_string(settings->GetHostRPKeyType(host));
		brls::ListItem* host_rp_key_type = new brls::ListItem("RP Key type");
		host_rp_key_type->setValue(host_rp_key_type_string.c_str());
		ls->addView(host_rp_key_type);

	}

	return true;
}

void MainApplication::BuildAddHostConfigurationMenu(brls::List * add_host)
{
	// create host for wan connection
	// brls::Label* add_host_label = new brls::Label(brls::LabelStyle::REGULAR,
	// 	"Add Host configuration", true);

	brls::ListItem* display_name = new brls::ListItem("Display name");
	auto display_name_cb = [this, display_name](brls::View * view)
	{
		char name[16] = {0};
		bool input = this->io->ReadUserKeyboard(name, sizeof(name));
		if(input)
		{
			// update gui
			display_name->setValue(name);
			// set internal value
			this->remote_display_name = name;
		}
	};
	display_name->getClickEvent()->subscribe(display_name_cb);
	add_host->addView(display_name);

	brls::ListItem* address = new brls::ListItem("Remote IP/name");
	auto address_cb = [this, address](brls::View * view)
	{
		char addr[256] = {0};
		bool input = this->io->ReadUserKeyboard(addr, sizeof(addr));
		if(input)
		{
			// update gui
			address->setValue(addr);
			// set internal value
			this->remote_addr = addr;
		}
	};
	address->getClickEvent()->subscribe(address_cb);
	add_host->addView(address);


	// TODO
	// brls::ListItem* port = new brls::ListItem("Remote session port",  "tcp/udp 9295");
	// brls::ListItem* port = new brls::ListItem("Remote stream port",  "udp 9296");
	// brls::ListItem* port = new brls::ListItem("Remote Senkusha port",  "udp 9297");
	brls::SelectListItem* ps_version = new brls::SelectListItem("PlayStation Version",
					{ "PS5", "PS4 > 8", "7 < PS4 < 8",  "PS4 < 7"});
	auto ps_version_cb = [this, ps_version](int result)
	{
		switch(result)
		{
			case 0:
				// ps5 v1
				this->remote_ps_version = CHIAKI_TARGET_PS5_1;
				break;
			case 1:
				// ps4 v8
				this->remote_ps_version = CHIAKI_TARGET_PS4_10;
				break;
			case 2:
				// ps4 v7
				this->remote_ps_version = CHIAKI_TARGET_PS4_9;
				break;
			case 3:
				// ps4 v6
				this->remote_ps_version = CHIAKI_TARGET_PS4_8;
				break;
		}
	};
	ps_version->getValueSelectedEvent()->subscribe(ps_version_cb);
	add_host->addView(ps_version);

	brls::ListItem* register_host = new brls::ListItem("Register");
	auto register_host_cb = [this](brls::View * view)
	{
		bool err = false;
		if(this->remote_display_name.length() <= 0)
		{
			brls::Application::notify("No Display name defined");
			err = true;
		}

		if(this->remote_addr.length() <= 0)
		{
			brls::Application::notify("No Remote address provided");
			err = true;
		}

		if(this->remote_ps_version < 0)
		{
			brls::Application::notify("No PlayStation Version provided");
			err = true;
		}

		if(err)
			return;

		Host * host = this->settings->GetOrCreateHost(&this->remote_display_name);
		host->SetHostAddr(this->remote_addr);
		host->SetChiakiTarget(this->remote_ps_version);

		HostInterface::Register(this->io, host, this->settings);
	};
	register_host->getClickEvent()->subscribe(register_host_cb);

	add_host->addView(register_host);
}

PSRemotePlay::PSRemotePlay(IO * io, Host * host)
	: io(io), host(host)
{
	// store joycon/touchpad keys
	for(int x=0; x < CHIAKI_CONTROLLER_TOUCHES_MAX; x++)
		// start touchpad as "untouched"
		this->state.touches[x].id = -1;

	// this->base_time=glfwGetTime();
}

void PSRemotePlay::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
	this->io->MainLoop(&this->state);
	this->host->SendFeedbackState(&state);

	// FPS calculation
	// this->frame_counter += 1;
	// double frame_time = glfwGetTime();
	// if((frame_time - base_time) >= 1.0)
	// {
	// 	base_time += 1;
	// 	//printf("FPS: %d\n", this->frame_counter);
	// 	this->fps = this->frame_counter;
	// 	this->frame_counter = 0;
	// }
	// nvgBeginPath(vg);
	// nvgFillColor(vg, nvgRGBA(255,192,0,255));
	// nvgFontFaceId(vg, ctx->fontStash->regular);
	// nvgFontSize(vg, style->Label.smallFontSize);
	// nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	// char fps_str[9] = {0};
	// sprintf(fps_str, "FPS: %000d", this->fps);
	// nvgText(vg, 5,10, fps_str, NULL);
}

PSRemotePlay::~PSRemotePlay()
{
}

