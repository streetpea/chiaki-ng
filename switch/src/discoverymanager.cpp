// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

#include <discoverymanager.h>

#define PING_MS 500
#define HOSTS_MAX 16
#define DROP_PINGS 3

static void Discovery(ChiakiDiscoveryHost *discovered_hosts, size_t hosts_count, void *user)
{
	DiscoveryManager *dm = (DiscoveryManager *)user;
	for(size_t i = 0; i < hosts_count; i++)
	{
		dm->DiscoveryCB(discovered_hosts + i);
	}
}

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

DiscoveryManager::DiscoveryManager()
{
	this->settings = Settings::GetInstance();
	this->log = this->settings->GetLogger();
}

DiscoveryManager::~DiscoveryManager()
{
	// join discovery thread
	if(this->service_enable)
		SetService(false);
}

void DiscoveryManager::SetService(bool enable)
{
	if(this->service_enable == enable)
		return;

	this->service_enable = enable;

	if(enable)
	{
		IfAddrs addresses = GetIPv4BroadcastAddr();
		ChiakiDiscoveryServiceOptions options;
		options.ping_ms = PING_MS;
		options.ping_initial_ms = PING_MS;
		options.hosts_max = HOSTS_MAX;
		options.host_drop_pings = DROP_PINGS;
		options.cb = Discovery;
		options.cb_user = this;

		sockaddr_in addr_broadcast = {};
		addr_broadcast.sin_family = AF_INET;
		addr_broadcast.sin_addr.s_addr = addresses.broadcast;
		options.broadcast_addrs = (struct sockaddr_storage *)malloc(sizeof(struct sockaddr_storage));
		memcpy(options.broadcast_addrs, &addr_broadcast, sizeof(addr_broadcast));		
		options.broadcast_num = 1;
			
		// Base broadcast address (255.255.255.255)
		struct sockaddr_in in_addr = {};
		in_addr.sin_family = AF_INET;
		in_addr.sin_addr.s_addr = 0xffffffff;
		struct sockaddr_storage addr;
		memcpy(&addr, &in_addr, sizeof(in_addr));
		options.send_addr = &addr;
		options.send_addr_size = sizeof(in_addr);
		options.send_host = nullptr;

		ChiakiErrorCode err = chiaki_discovery_service_init(&this->service, &options, log);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			this->service_enable = false;
			CHIAKI_LOGE(this->log, "DiscoveryManager failed to init Discovery Service");
			return;
		}
	}
	else
	{
		chiaki_discovery_service_fini(&this->service);
	}
}


IfAddrs DiscoveryManager::GetIPv4BroadcastAddr()
{
	IfAddrs result;
	uint32_t current_addr, subnet_mask;
	// init nintendo net interface service
	Result rc = nifmInitialize(NifmServiceType_User);
	if(R_SUCCEEDED(rc))
	{
		// read current IP and netmask
		rc = nifmGetCurrentIpConfigInfo(
			&current_addr, &subnet_mask,
			NULL, NULL, NULL);
		nifmExit();
	}
	else
	{
		CHIAKI_LOGE(this->log, "Failed to get nintendo nifmGetCurrentIpConfigInfo");
		return result;
	}
	result.broadcast = current_addr | (~subnet_mask);
	result.local = current_addr;
	return result;
}


int DiscoveryManager::Send(struct sockaddr *host_addr, size_t host_addr_len)
{
	if(!host_addr)
	{
		CHIAKI_LOGE(log, "Null sockaddr");
		return 1;
	}
	ChiakiDiscoveryPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;

	chiaki_discovery_send(&this->discovery, &packet, this->host_addr, this->host_addr_len);
	return 0;
}

int DiscoveryManager::Send(const char *discover_ip_dest)
{
	struct addrinfo *host_addrinfos;
	int r = getaddrinfo(discover_ip_dest, NULL, NULL, &host_addrinfos);
	if(r != 0)
	{
		CHIAKI_LOGE(log, "getaddrinfo failed");
		return 1;
	}

	struct sockaddr *host_addr = nullptr;
	socklen_t host_addr_len = 0;

	for(struct addrinfo *ai = host_addrinfos; ai; ai = ai->ai_next)
	{
		if(ai->ai_protocol != IPPROTO_UDP)
			continue;
		if(ai->ai_family != AF_INET) // TODO: IPv6
			continue;

		this->host_addr_len = ai->ai_addrlen;
		this->host_addr = (struct sockaddr *)malloc(host_addr_len);
		if(!this->host_addr)
			break;
		memcpy(this->host_addr, ai->ai_addr, this->host_addr_len);
	}

	freeaddrinfo(host_addrinfos);

	if(!this->host_addr)
	{
		CHIAKI_LOGE(log, "Failed to get addr for hostname");
		return 1;
	}
	return DiscoveryManager::Send(this->host_addr, this->host_addr_len);
}

int DiscoveryManager::Send()
{
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = GetIPv4BroadcastAddr().broadcast;

	this->host_addr_len = sizeof(sockaddr_in);
	this->host_addr = (struct sockaddr *)malloc(host_addr_len);
	memcpy(this->host_addr, &addr, this->host_addr_len);

	return DiscoveryManager::Send(this->host_addr, this->host_addr_len);
}

void DiscoveryManager::DiscoveryCB(ChiakiDiscoveryHost *discovered_host)
{
	// the user ptr is passed as
	// chiaki_discovery_thread_start arg

	std::string key = discovered_host->host_name;
	Host *host = this->settings->GetOrCreateHost(&key);

	CHIAKI_LOGI(this->log, "--");
	CHIAKI_LOGI(this->log, "Discovered Host:");
	CHIAKI_LOGI(this->log, "State:                             %s", chiaki_discovery_host_state_string(discovered_host->state));

	host->state = discovered_host->state;
	host->discovered = true;

	// add host ptr to list
	if(discovered_host->system_version && discovered_host->device_discovery_protocol_version)
	{
		// example: 07020001
		ChiakiTarget target = chiaki_discovery_host_system_version_target(discovered_host);
		host->SetChiakiTarget(target);

		CHIAKI_LOGI(this->log, "System Version:                    %s", discovered_host->system_version);
		CHIAKI_LOGI(this->log, "Device Discovery Protocol Version: %s", discovered_host->device_discovery_protocol_version);
		CHIAKI_LOGI(this->log, "PlayStation ChiakiTarget Version:  %d", target);
	}

	if(discovered_host->host_request_port)
		CHIAKI_LOGI(this->log, "Request Port:                      %hu", (unsigned short)discovered_host->host_request_port);

	if(discovered_host->host_addr)
	{
		host->host_addr = discovered_host->host_addr;
		CHIAKI_LOGI(this->log, "Host Addr:                         %s", discovered_host->host_addr);
	}

	if(discovered_host->host_name)
	{
		host->host_name = discovered_host->host_name;
		CHIAKI_LOGI(this->log, "Host Name:                         %s", discovered_host->host_name);
	}

	if(discovered_host->host_type)
		CHIAKI_LOGI(this->log, "Host Type:                         %s", discovered_host->host_type);

	if(discovered_host->host_id)
		CHIAKI_LOGI(this->log, "Host ID:                           %s", discovered_host->host_id);

	if(discovered_host->running_app_titleid)
		CHIAKI_LOGI(this->log, "Running App Title ID:              %s", discovered_host->running_app_titleid);

	if(discovered_host->running_app_name)
		CHIAKI_LOGI(this->log, "Running App Name:                  %s", discovered_host->running_app_name);

	CHIAKI_LOGI(this->log, "--");
}


void DiscoveryManager::makeRequest(const std::string& username, std::function<void(const std::string&)> successCallback, 
                std::function<void(const std::string&)> errorCallback) {
    CURL *curl = curl_easy_init();
    if(!curl) {
        errorCallback("Failed to initialize CURL");
        return;
    }

    char* encoded_username = curl_easy_escape(curl, username.c_str(), username.length());
    std::string url = "https://psn.flipscreen.games/search.php?username=" + std::string(encoded_username);
    curl_free(encoded_username);

    std::string response_data;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    CURLcode res = curl_easy_perform(curl);
    
    if(res != CURLE_OK) {
        errorCallback(curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return;
    }

    struct json_object *parsed_json;
    parsed_json = json_tokener_parse(response_data.c_str());
    
    if(!parsed_json) {
        errorCallback("Failed to parse JSON response");
        curl_easy_cleanup(curl);
        return;
    }

    // Extract encoded_id
    struct json_object *encoded_id;
    if(json_object_object_get_ex(parsed_json, "encoded_id", &encoded_id)) {
        // Success case - we found an encoded_id
        successCallback(json_object_get_string(encoded_id));
    } else {
        // Error case - look for error message
        struct json_object *error;
        if(json_object_object_get_ex(parsed_json, "error", &error)) {
            errorCallback(json_object_get_string(error));
        } else {
            errorCallback("Unknown error occurred");
        }
    }

    // Cleanup
    json_object_put(parsed_json);
    curl_easy_cleanup(curl);
}