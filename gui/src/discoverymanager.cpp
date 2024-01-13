// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <discoverymanager.h>
#include <exception.h>

#include <cstring>

#include "autoconnecthelper.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#define PING_MS		500
#define HOSTS_MAX	16
#define DROP_PINGS	3

HostMAC DiscoveryHost::GetHostMAC() const
{
	QByteArray data = QByteArray::fromHex(host_id.toUtf8());
	if(data.size() != 6)
		return HostMAC();
	return HostMAC((uint8_t *)data.constData());
}

static void DiscoveryServiceHostsCallback(bool queueResponses, ChiakiDiscoveryHost *hosts, size_t hosts_count, void *user);

DiscoveryManager::DiscoveryManager(QObject *parent, bool queueResponses) : QObject(parent), queueResponses(queueResponses)
{
	chiaki_log_init(&log, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE, chiaki_log_cb_print, nullptr);

	service_active = false;
}

DiscoveryManager::~DiscoveryManager()
{
	if(service_active)
		chiaki_discovery_service_fini(&service);
}

void DiscoveryManager::SetActive(bool active)
{
	if(service_active == active)
		return;
	service_active = active;

	if(active)
	{
		ChiakiDiscoveryServiceOptions options;
		options.queueResponses = queueResponses;
		options.ping_ms = PING_MS;
		options.hosts_max = HOSTS_MAX;
		options.host_drop_pings = DROP_PINGS;
		options.cb = DiscoveryServiceHostsCallback;
		options.cb_user = this;

		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = 0xffffffff; // 255.255.255.255
		options.send_addr = reinterpret_cast<sockaddr *>(&addr);
		options.send_addr_size = sizeof(addr);

		ChiakiErrorCode err = chiaki_discovery_service_init(&service, &options, &log);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			service_active = false;
			CHIAKI_LOGE(&log, "DiscoveryManager failed to init Discovery Service");
			return;
		}
	}
	else
	{
		chiaki_discovery_service_fini(&service);
		hosts = {};
		emit HostsUpdated();
	}

}

void DiscoveryManager::SendWakeup(const QString &host, const QByteArray &regist_key, bool ps5)
{
	QByteArray key = regist_key;
	for(size_t i=0; i<key.size(); i++)
	{
		if(!key.at(i))
		{
			key.resize(i);
			break;
		}
	}

	bool ok;
	uint64_t credential = (uint64_t)QString::fromUtf8(key).toULongLong(&ok, 16);
	if(key.size() > 8 || !ok)
	{
		CHIAKI_LOGE(&log, "DiscoveryManager got invalid regist key for wakeup");
		throw Exception("Invalid regist key");
	}

	ChiakiErrorCode err = chiaki_discovery_wakeup(&log, service_active ? &service.discovery : nullptr, host.toUtf8().constData(), credential, ps5);

	if(err != CHIAKI_ERR_SUCCESS)
		throw Exception(QString("Failed to send Packet: %1").arg(chiaki_error_string(err)));
}

void DiscoveryManager::DiscoveryServiceHosts(QList<DiscoveryHost> hosts)
{
	this->hosts = std::move(hosts);
	emit HostsUpdated();
}

class DiscoveryManagerPrivate
{
	public:
		static void DiscoveryServiceHosts(bool queueResponses, DiscoveryManager *discovery_manager, const QList<DiscoveryHost> &hosts) {
			if (queueResponses){
				QMetaObject::invokeMethod(discovery_manager, "DiscoveryServiceHosts", Qt::ConnectionType::QueuedConnection, Q_ARG(QList<DiscoveryHost>, hosts));
			} else {
			 	discovery_manager->DiscoveryServiceHosts(hosts);
			}
		}
};

static void DiscoveryServiceHostsCallback(bool queueResponses, ChiakiDiscoveryHost *hosts, size_t hosts_count, void *user)
{
	QList<DiscoveryHost> hosts_list;
	hosts_list.reserve(hosts_count);

	for(size_t i=0; i<hosts_count; i++)
	{
		ChiakiDiscoveryHost *h = hosts + i;
		DiscoveryHost o = {};
		o.ps5 = chiaki_discovery_host_is_ps5(h);
		o.state = h->state;
		o.host_request_port = o.host_request_port;
#define CONVERT_STRING(name) if(h->name) { o.name = QString::fromLocal8Bit(h->name); }
		CHIAKI_DISCOVERY_HOST_STRING_FOREACH(CONVERT_STRING)
#undef CONVERT_STRING
		hosts_list.append(o);
	}

	DiscoveryManagerPrivate::DiscoveryServiceHosts(queueResponses, reinterpret_cast<DiscoveryManager *>(user), hosts_list);
}

static void discovery_cb(ChiakiDiscoveryHost *host, void *user) {
	DiscoveryManager *manager = static_cast<DiscoveryManager*>(user);
	if (manager) {
		manager->last_state = host->state;
	}
}

void DiscoveryManager::discoverHostState(QString addr) {
	ChiakiDiscovery discovery;
    ChiakiErrorCode err = chiaki_discovery_init(&discovery, &log, AF_INET); // TODO: IPv6
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(&log, "Discovery init failed");
        return;
    }

    ChiakiDiscoveryThread thread;

    err = chiaki_discovery_thread_start_oneshot(&thread, &discovery, discovery_cb, this);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(&log, "Discovery thread init failed");
        chiaki_discovery_fini(&discovery);
        return;
    }

    struct addrinfo *host_addrinfos;
    int r = getaddrinfo(addr.toStdString().c_str(), NULL, NULL, &host_addrinfos);
    if(r != 0)
    {
        CHIAKI_LOGE(&log, "getaddrinfo failed");
        return;
    }

    struct sockaddr *host_addr = NULL;
    socklen_t host_addr_len = 0;
    for(struct addrinfo *ai=host_addrinfos; ai; ai=ai->ai_next)
    {
        if(ai->ai_protocol != IPPROTO_UDP)
            continue;
        if(ai->ai_family != AF_INET) // TODO: IPv6
            continue;

        host_addr_len = ai->ai_addrlen;
        host_addr = (struct sockaddr *)malloc(host_addr_len);
        if(!host_addr)
            break;
        memcpy(host_addr, ai->ai_addr, host_addr_len);
    }
    freeaddrinfo(host_addrinfos);

    if(!host_addr)
    {
        CHIAKI_LOGE(&log, "Failed to get addr for hostname");
        return;
    }

    float timeout_sec = 2;

    ChiakiDiscoveryPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;
    packet.protocol_version = CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4;
    ((struct sockaddr_in *)host_addr)->sin_port = htons(CHIAKI_DISCOVERY_PORT_PS4);
    err = chiaki_discovery_send(&discovery, &packet, host_addr, host_addr_len);
    if(err != CHIAKI_ERR_SUCCESS)
        CHIAKI_LOGE(&log, "Failed to send discovery packet for PS4: %s", chiaki_error_string(err));
    packet.protocol_version = CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5;
    ((struct sockaddr_in *)host_addr)->sin_port = htons(CHIAKI_DISCOVERY_PORT_PS5);
    err = chiaki_discovery_send(&discovery, &packet, host_addr, host_addr_len);
    if(err != CHIAKI_ERR_SUCCESS)
        CHIAKI_LOGE(&log, "Failed to send discovery packet for PS5: %s", chiaki_error_string(err));
    uint64_t timeout_ms=(timeout_sec * 1000);
    err = chiaki_thread_timedjoin(&thread.thread, NULL, timeout_ms);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        if(err == CHIAKI_ERR_TIMEOUT)
        {
            CHIAKI_LOGE(&log, "Discovery request timed out after timeout: %.*f seconds", 1, timeout_sec);
            chiaki_discovery_thread_stop(&thread);
        }
        chiaki_discovery_fini(&discovery);
        return;
    }
    chiaki_discovery_fini(&discovery);
}
