// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "gui.h"
#include <chiaki/log.h>
#include "views/enter_pin_view.h"
#include "views/ps_remote_play.h"



#define SCREEN_W 1280
#define SCREEN_H 720

// TODO
using namespace brls::i18n::literals; // for _i18n

#define DIALOG(dialog, r)                                                       \
	brls::Dialog *d_##dialog = new brls::Dialog(r);                             \
	brls::GenericEvent::Callback cb_##dialog = [d_##dialog](brls::View *view) { \
		d_##dialog->close();                                                    \
	};                                                                          \
	d_##dialog->addButton("Ok", cb_##dialog);                                   \
	d_##dialog->setCancelable(false);                                           \
	d_##dialog->open();                                                         \
	brls::Logger::info("Dialog: {0}", r);

HostInterface::HostInterface(Host *host)
	: host(host)
{
	this->settings = Settings::GetInstance();
	this->log = this->settings->GetLogger();
	this->io = IO::GetInstance();

	brls::ListItem *connect = new brls::ListItem("Connect");
	connect->getClickEvent()->subscribe(std::bind(&HostInterface::Connect, this, std::placeholders::_1));
	this->addView(connect);

	brls::ListItem *wakeup = new brls::ListItem("Wakeup");
	wakeup->getClickEvent()->subscribe(std::bind(&HostInterface::Wakeup, this, std::placeholders::_1));
	this->addView(wakeup);

	// message delimiter
	brls::Label *info = new brls::Label(brls::LabelStyle::REGULAR,
		"Host configuration", true);
	this->addView(info);

	// push opengl chiaki stream
	// when the host is connected
	this->host->SetEventConnectedCallback(std::bind(&HostInterface::Stream, this));
	this->host->SetEventQuitCallback(std::bind(&HostInterface::CloseStream, this, std::placeholders::_1));
	// push login pin view onto the stack in callback fn
	this->host->SetEventLoginPinRequestCallback(std::bind(&HostInterface::EnterPin, this, std::placeholders::_1));
	// allow host to update controller state
	this->host->SetEventRumbleCallback(std::bind(&IO::SetRumble, this->io, std::placeholders::_1, std::placeholders::_2));
	this->host->SetReadControllerCallback(std::bind(&IO::UpdateControllerState, this->io, std::placeholders::_1, std::placeholders::_2));
}

HostInterface::~HostInterface()
{
	Disconnect();
}

void HostInterface::Register(Host *host, std::function<void()> success_cb)
{
	Settings *settings = Settings::GetInstance();
	IO *io = IO::GetInstance();

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
	auto event_type_finished_success_cb = [settings, success_cb]() {
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

	auto event_type_finished_failed_cb = []() {
		// unlock user inputs
		brls::Application::unblockInputs();
		brls::Application::notify("Registration failed");
	};
	host->SetRegistEventTypeFinishedFailed(event_type_finished_failed_cb);

	// the host is not registered yet
	// use callback to ensure that the message is showed on screen
	// before the Swkbd
	auto pin_input_cb = [host](int pin) {
		// prevent users form messing with the gui
		brls::Application::blockInputs();
		int ret = host->Register(pin);
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
	};
	// the pin is 8 digit
	bool success = brls::Swkbd::openForNumber(pin_input_cb,
		"Please enter your PlayStation registration PIN code", "8 digits without spaces", 8, "", "", "");
}

void HostInterface::Register()
{
	// use Connect just after the registration to save user inputs
	HostInterface::Register(this->host, std::bind(&HostInterface::ConnectSession, this));
}

void HostInterface::Wakeup(brls::View *view)
{
	if(!this->host->HasRPkey())
	{
		// the host is not registered yet
		this->Register();
	}
	else
	{
		int r = host->Wakeup();
		if(r == 0)
		{
			brls::Application::notify("Wakeup packet sent to PlayStation");
		}
		else
		{
			brls::Application::notify("Wakeup packet sent, no response from PlayStation");
		}
	}
}

void HostInterface::Connect(brls::View *view)
{
	// check that all requirements are met
	if(!this->host->IsDiscovered() && !this->host->HasRPkey())
	{
		// at this point the host must be discovered or registered manually
		// to validate the system_version accuracy
		brls::Application::crash("Undefined PlayStation remote version");
	}

	// ignore state for remote hosts
	if(this->host->IsDiscovered() && !this->host->IsReady())
	{
		// host in standby mode
		DIALOG(ptoyp, "Your PlayStation is off, please turn it on");
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
	CHIAKI_LOGI(this->log, "Session initiated");
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

void HostInterface::Stream()
{
	this->connected = true;
	// https://github.com/natinusala/borealis/issues/59
	// disable 60 fps limit
	brls::Application::setMaximumFPS(0);

	// show FPS counter
	// brls::Application::setDisplayFramerate(true);

	// push raw opengl stream over borealis
	brls::Application::pushView(new PSRemotePlay(this->host));
}

void HostInterface::CloseStream(ChiakiQuitEvent *quit)
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
}

void HostInterface::EnterPin(bool isError) 
{
	// enter pin callback,
	// inputs were blocked in ConnectSession
	brls::Application::unblockInputs();
	// if this is triggered as a result 
	if(isError){
		brls::Application::notify("Wrong PIN");
	}
	
	brls::Application::pushView(new EnterPinView(this->host, isError));
}

MainApplication::MainApplication(DiscoveryManager *discoverymanager)
	: discoverymanager(discoverymanager)
{
	this->settings = Settings::GetInstance();
	this->log = this->settings->GetLogger();
	this->io = IO::GetInstance();
}

MainApplication::~MainApplication()
{
	this->discoverymanager->SetService(false);
	this->io->FreeController();
	this->io->FreeVideo();
}

bool MainApplication::Load()
{
	this->discoverymanager->SetService(true);
	// Init the app
	brls::Logger::setLogLevel(brls::LogLevel::DEBUG);

	brls::i18n::loadTranslations();
	if(!brls::Application::init("Chiaki Remote play"))
	{
		brls::Logger::error("Unable to init Borealis application");
		return false;
	}

	// init chiaki gl after borealis
	// let borealis manage the main screen/window
	// if(!io->InitVideo(0, 0, SCREEN_W, SCREEN_H))
	// {
	// 	brls::Logger::error("Failed to initiate Video");
	// }

	brls::Logger::info("Load SDL/HiD controller");
	if(!io->InitController())
	{
		brls::Logger::error("Failed to initiate Controller");
	}

	// Create a view
	this->rootFrame = new brls::TabFrame();
	this->rootFrame->setTitle("Chiaki-ng: Open Source PlayStation Remote Play Client");
	this->rootFrame->setIcon(BOREALIS_ASSET("icon.png"));

	brls::List *config = new brls::List();
	brls::List *add_host = new brls::List();
	BuildConfigurationMenu(config);
	BuildAddHostConfigurationMenu(add_host);

	std::map<std::string, Host> *hosts = this->settings->GetHostsMap();
	for (auto it = hosts->begin(); it != hosts->end(); ++it)
	{
		if (this->host_menuitems.find(&it->second) == this->host_menuitems.end() &&
		    (it->second.HasRPkey() || it->second.IsDiscovered()))
		{
		HostInterface *new_host = new HostInterface(&it->second);
		this->host_menuitems[&it->second] = new_host;
		BuildConfigurationMenu(new_host, &it->second);
		this->rootFrame->addTab(it->second.GetHostName().c_str(), new_host);
		}
	}

	// Static tabs under the registered targets
	this->rootFrame->addSeparator();
	this->rootFrame->addTab("Configuration", config);
	this->rootFrame->addTab("Add Manual Host", add_host);

	brls::Application::pushView(this->rootFrame);

	while (brls::Application::mainLoop()) {
	}
	
	return true;
}

bool MainApplication::BuildConfigurationMenu(brls::List *ls, Host *host)
{
	std::string psn_account_id_string = this->settings->GetPSNAccountID(host);
	brls::InputListItem *psn_account_id = new brls::InputListItem("PSN Account ID", psn_account_id_string,
		"Account ID in base64 format", "PS5 or PS4 System Software version 7.00 and greater", CHIAKI_PSN_ACCOUNT_ID_SIZE * 2,
		brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_SPACE |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_AT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_PERCENT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_BACKSLASH);

	auto psn_account_id_cb = [this, host, psn_account_id](brls::View *view) {
		// retrieve, push and save setting
		this->settings->SetPSNAccountID(host, psn_account_id->getValue());
		// write on disk
		this->settings->WriteFile();
	};
	psn_account_id->getClickEvent()->subscribe(psn_account_id_cb);
	ls->addView(psn_account_id);

	
	brls::InputListItem *lookup_account_id = new brls::InputListItem("Lookup Account ID by username",
		"", "PSN Account name", "Only works if the PSN account's privacy settings are set to public!", 16,
		brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_SPACE |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_AT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_PERCENT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_FORWSLASH |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_BACKSLASH);

	auto lookup_account_id_cb = [this, host, lookup_account_id, psn_account_id](brls::View *view) {
		// lookup psn_online
		std::string entered_value = lookup_account_id->getValue();
		this->discoverymanager->makeRequest(entered_value, 
			 [this, host, lookup_account_id, psn_account_id](const std::string& accountId) {
				// retrieve, push and save setting
				CHIAKI_LOGI(this->log, fmt::format("Found account id {}", accountId).c_str());
				this->settings->SetPSNAccountID(host, std::string(accountId));
				// write on disk
				this->settings->WriteFile();
				lookup_account_id->setValue("");
				// set the view of the other field,
				psn_account_id->setValue(std::string(accountId));
			},
			[this, lookup_account_id, entered_value](const std::string& error) {
				CHIAKI_LOGE(this->log, error.c_str());
				lookup_account_id->setValue("");
				DIALOG(upaid, fmt::format("Unable to fetch account id for {}", entered_value));
			}
		);
	};
	lookup_account_id->getClickEvent()->subscribe(lookup_account_id_cb);
	ls->addView(lookup_account_id);


	std::string psn_online_id_string = this->settings->GetPSNOnlineID(host);
	brls::InputListItem *psn_online_id = new brls::InputListItem("PSN Online ID",
		psn_online_id_string, "", "", 16,
		brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_SPACE |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_AT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_PERCENT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_FORWSLASH |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_BACKSLASH);

	auto psn_online_id_cb = [this, host, psn_online_id](brls::View *view) {
		// retrieve, push and save setting
		this->settings->SetPSNOnlineID(host, psn_online_id->getValue());
		// write on disk
		this->settings->WriteFile();
	};
	psn_online_id->getClickEvent()->subscribe(psn_online_id_cb);
	ls->addView(psn_online_id);


	int value;
	ChiakiVideoResolutionPreset resolution_preset = this->settings->GetVideoResolution(host);
	switch(resolution_preset)
	{
		case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
			value = 0;
			break;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
			value = 1;
			break;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
			value = 2;
			break;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
			value = 3;
			break;
	}

	brls::SelectListItem *resolution = new brls::SelectListItem(
		"Resolution", { "1080p (PS5 and PS4 Pro only)", "720p", "540p", "360p" }, value);

	auto resolution_cb = [this, host](int result) {
		ChiakiVideoResolutionPreset value = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
		switch(result)
		{
			case 0:
				value = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
				break;
			case 1:
				value = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
				break;
			case 2:
				value = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
				break;
			case 3:
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

	brls::SelectListItem *fps = new brls::SelectListItem(
		"FPS", { "60fps", "30fps" }, value);

	auto fps_cb = [this, host](int result) {
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

	value = this->settings->GetHaptic(host);

	brls::SelectListItem *haptic = new brls::SelectListItem(
		"Haptics", { "Disabled", "Weak", "Strong" }, value);

	auto haptic_cb = [this, host](int result) {
		HapticPreset value = HAPTIC_PRESET_DIABLED;
		switch(result)
		{
			case 0:
				value = HAPTIC_PRESET_DIABLED;
				break;
			case 1:
				value = HAPTIC_PRESET_WEAK;
				break;
			case 2:
				value = HAPTIC_PRESET_STRONG;
				break;
		}
		this->settings->SetHaptic(host, value);
		this->settings->WriteFile();
	};

	haptic->getValueSelectedEvent()->subscribe(haptic_cb);
	ls->addView(haptic);

	if(host != nullptr)
	{	
	    brls::Label *info = new brls::Label(brls::LabelStyle::REGULAR,
		"Host information", true);
	    ls->addView(info);
	
	    std::string host_name_string = this->settings->GetHostName(host);
	    brls::ListItem *host_name = new brls::ListItem("PS Hostname");
	    host_name->setValue(host_name_string.c_str());
	    ls->addView(host_name);
	
	    std::string host_addr_string = settings->GetHostAddr(host);		
		brls::InputListItem *host_addr = new brls::InputListItem("Remote IP/name",
		host_addr_string, "IP address or fqdn", "", 255,
		brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_SPACE |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_AT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_PERCENT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_FORWSLASH |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_BACKSLASH);		
		
	    auto host_addr_cb = [this, host, host_addr](brls::View *view)
		{
			std::string new_addr = host_addr->getValue();	
			this->settings->SetHostAddr(host, new_addr);	
			this->settings->WriteFile();		
			brls::Application::notify("PS Address updated successfully");
	    };
	
	    host_addr->getClickEvent()->subscribe(host_addr_cb);
	    ls->addView(host_addr);

		brls::ListItem *host_regist_state_item = new brls::ListItem("Register Status");
		host_regist_state_item->setValue(!settings->GetHostRPKey(host).empty() ? "registered" : "unregistered");
		ls->addView(host_regist_state_item);
	}

	return true;
}

void MainApplication::BuildAddHostConfigurationMenu(brls::List *add_host)
{
	// create host for wan connection
	// brls::Label* add_host_label = new brls::Label(brls::LabelStyle::REGULAR,
	// 	"Add Host configuration", true);

	brls::InputListItem *display_name = new brls::InputListItem("Display name",
		"default", "configuration name", "", 16,
		brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_SPACE |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_AT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_PERCENT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_FORWSLASH |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_BACKSLASH);

	add_host->addView(display_name);

	brls::InputListItem *address = new brls::InputListItem("Remote IP/name",
		"", "IP address or fqdn", "", 255,
		brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_SPACE |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_AT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_PERCENT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_FORWSLASH |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_BACKSLASH);

	add_host->addView(address);

	// TODO
	// brls::ListItem* port = new brls::ListItem("Remote session port",  "tcp/udp 9295");
	// brls::ListItem* port = new brls::ListItem("Remote stream port",  "udp 9296");
	// brls::ListItem* port = new brls::ListItem("Remote Senkusha port",  "udp 9297");
	brls::SelectListItem *ps_version = new brls::SelectListItem("Target PlayStation System",
		{ "PS5", "PS4 System Version > 8.00", "7.00 < PS4 System Version < 8.00", "PS4 System Version < 7.00" }); //This may look like a pointless change but it does actually make things a bit clearer for the user
	add_host->addView(ps_version);

	brls::ListItem *register_host = new brls::ListItem("Register");
	auto register_host_cb = [this, display_name, address, ps_version](brls::View *view) {
		bool err = false;
		std::string dn = display_name->getValue();
		std::string addr = address->getValue();
		ChiakiTarget version = CHIAKI_TARGET_PS4_UNKNOWN;

		switch(ps_version->getSelectedValue())
		{
			case 0:
				// ps5 v1
				version = CHIAKI_TARGET_PS5_1;
				break;
			case 1:
				// ps4 v8
				version = CHIAKI_TARGET_PS4_10;
				break;
			case 2:
				// ps4 v7
				version = CHIAKI_TARGET_PS4_9;
				break;
			case 3:
				// ps4 v6
				version = CHIAKI_TARGET_PS4_8;
				break;
		}

		if(dn.length() <= 0)
		{
			brls::Application::notify("No Display name defined");
			err = true;
		}

		if(addr.length() <= 0)
		{
			brls::Application::notify("No Remote address provided");
			err = true;
		}

		if(version <= CHIAKI_TARGET_PS4_UNKNOWN)
		{
			brls::Application::notify("No PlayStation Version provided");
			err = true;
		}

		if(err)
			return;

		Host *host = this->settings->GetOrCreateHost(&dn);
		host->SetHostAddr(addr);
		host->SetChiakiTarget(version);
		HostInterface::Register(host);
	};
	register_host->getClickEvent()->subscribe(register_host_cb);

	add_host->addView(register_host);
}

