// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SETTINGS_H
#define CHIAKI_SETTINGS_H

#include <regex>

#include <chiaki/log.h>
#include "host.h"

// mutual host and settings
class Host;

class Settings
{
	protected:
		// keep constructor private (sigleton class)
		Settings();
		static Settings * instance;

	private:
		const char * filename = "chiaki.conf";
		ChiakiLog log;
		std::map<std::string, Host> hosts;

		// global_settings from psedo INI file
		ChiakiVideoResolutionPreset global_video_resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
		ChiakiVideoFPSPreset global_video_fps = CHIAKI_VIDEO_FPS_PRESET_60;
		std::string global_psn_online_id = "";
		std::string global_psn_account_id = "";

		typedef enum configurationitem
		{
			UNKNOWN,
			HOST_NAME,
			HOST_ADDR,
			PSN_ONLINE_ID,
			PSN_ACCOUNT_ID,
			RP_KEY,
			RP_KEY_TYPE,
			RP_REGIST_KEY,
			VIDEO_RESOLUTION,
			VIDEO_FPS,
			TARGET,
		} ConfigurationItem;

		// dummy parser implementation
		// the aim is not to have bulletproof parser
		// the goal is to read/write inernal flat configuration file
		const std::map<Settings::ConfigurationItem, std::regex> re_map = {
			{HOST_NAME, std::regex("^\\[\\s*(.+)\\s*\\]")},
			{HOST_ADDR, std::regex("^\\s*host_(?:ip|addr)\\s*=\\s*\"?((\\d+\\.\\d+\\.\\d+\\.\\d+)|([A-Za-z0-9-]{1,255}))\"?")},
			{PSN_ONLINE_ID, std::regex("^\\s*psn_online_id\\s*=\\s*\"?([\\w_-]+)\"?")},
			{PSN_ACCOUNT_ID, std::regex("^\\s*psn_account_id\\s*=\\s*\"?([\\w/=+]+)\"?")},
			{RP_KEY, std::regex("^\\s*rp_key\\s*=\\s*\"?([\\w/=+]+)\"?")},
			{RP_KEY_TYPE, std::regex("^\\s*rp_key_type\\s*=\\s*\"?(\\d)\"?")},
			{RP_REGIST_KEY, std::regex("^\\s*rp_regist_key\\s*=\\s*\"?([\\w/=+]+)\"?")},
			{VIDEO_RESOLUTION, std::regex("^\\s*video_resolution\\s*=\\s*\"?(1080p|720p|540p|360p)\"?")},
			{VIDEO_FPS, std::regex("^\\s*video_fps\\s*=\\s*\"?(60|30)\"?")},
			{TARGET, std::regex("^\\s*target\\s*=\\s*\"?(\\d+)\"?")},
		};

		ConfigurationItem ParseLine(std::string * line, std::string * value);
		size_t GetB64encodeSize(size_t);

	public:
		// singleton configuration
		Settings(const Settings&) = delete;
		void operator=(const Settings&) = delete;
		static Settings * GetInstance();

		ChiakiLog * GetLogger();
		std::map<std::string, Host> * GetHostsMap();
		Host * GetOrCreateHost(std::string * host_name);

		void ParseFile();
		int WriteFile();

		std::string ResolutionPresetToString(ChiakiVideoResolutionPreset resolution);
		int ResolutionPresetToInt(ChiakiVideoResolutionPreset resolution);
		ChiakiVideoResolutionPreset StringToResolutionPreset(std::string value);

		std::string FPSPresetToString(ChiakiVideoFPSPreset fps);
		int FPSPresetToInt(ChiakiVideoFPSPreset fps);
		ChiakiVideoFPSPreset StringToFPSPreset(std::string value);

		std::string GetHostName(Host * host);
		std::string GetHostAddr(Host * host);

		std::string GetPSNOnlineID(Host * host);
		void SetPSNOnlineID(Host * host, std::string psn_online_id);

		std::string GetPSNAccountID(Host * host);
		void SetPSNAccountID(Host * host, std::string psn_account_id);

		ChiakiVideoResolutionPreset GetVideoResolution(Host * host);
		void SetVideoResolution(Host * host, ChiakiVideoResolutionPreset value);
		void SetVideoResolution(Host * host, std::string value);

		ChiakiVideoFPSPreset GetVideoFPS(Host * host);
		void SetVideoFPS(Host * host, ChiakiVideoFPSPreset value);
		void SetVideoFPS(Host * host, std::string value);

		ChiakiTarget GetChiakiTarget(Host * host);
		bool SetChiakiTarget(Host * host, ChiakiTarget target);
		bool SetChiakiTarget(Host * host, std::string value);

		std::string GetHostRPKey(Host * host);
		bool SetHostRPKey(Host * host, std::string rp_key_b64);

		std::string GetHostRPRegistKey(Host * host);
		bool SetHostRPRegistKey(Host * host, std::string rp_regist_key_b64);

		int GetHostRPKeyType(Host * host);
		bool SetHostRPKeyType(Host * host, std::string value);
};

#endif // CHIAKI_SETTINGS_H
