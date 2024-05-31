// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <discoverymanager.h>
#include <exception.h>
#include <settings.h>

#include <cstring>

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

static void DiscoveryServiceHostsCallback(ChiakiDiscoveryHost *hosts, size_t hosts_count, void *user);
static void DiscoveryServiceHostsManualCallback(ChiakiDiscoveryHost *hosts, size_t hosts_count, void *user);

DiscoveryManager::DiscoveryManager(QObject *parent) : QObject(parent)
{
	chiaki_log_init(&log, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE, chiaki_log_cb_print, nullptr);

	service_active = false;
}

DiscoveryManager::~DiscoveryManager()
{
	if(service_active)
	{
		chiaki_discovery_service_fini(&service);
		chiaki_discovery_service_fini(&service_ipv6);
	}
	qDeleteAll(manual_services);
}

void DiscoveryManager::SetActive(bool active)
{
	if(service_active == active)
		return;
	service_active = active;

	if(active)
	{
		ChiakiDiscoveryServiceOptions options = {};
		options.ping_ms = PING_MS;
		options.hosts_max = HOSTS_MAX;
		options.host_drop_pings = DROP_PINGS;
		options.cb = DiscoveryServiceHostsCallback;
		options.cb_user = this;

		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = 0xffffffff; // 255.255.255.255
		options.send_addr = reinterpret_cast<sockaddr_in6 *>(&addr);
		options.send_addr_size = sizeof(addr);

		ChiakiDiscoveryServiceOptions options_ipv6 = {};
		options_ipv6.ping_ms = PING_MS;
		options_ipv6.hosts_max = HOSTS_MAX;
		options_ipv6.host_drop_pings = DROP_PINGS;
		options_ipv6.cb = DiscoveryServiceHostsCallback;
		options_ipv6.cb_user = this;
		sockaddr_in6 addr_ipv6 = {};
		addr_ipv6.sin6_family = AF_INET6;
		inet_pton(AF_INET6, "FF02::1", &addr_ipv6.sin6_addr);
		options_ipv6.send_addr = &addr_ipv6;
		options_ipv6.send_addr_size = sizeof(addr_ipv6);

		ChiakiErrorCode err = chiaki_discovery_service_init(&service, &options, &log);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			service_active = false;
			CHIAKI_LOGE(&log, "DiscoveryManager failed to init Discovery Service IPV4");
			return;
		}
		err = chiaki_discovery_service_init(&service_ipv6, &options_ipv6, &log);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			service_active = false;
			CHIAKI_LOGE(&log, "DiscoveryManager failed to init Discovery Service IPV6");
			return;
		}

		UpdateManualServices();
	}
	else
	{
		chiaki_discovery_service_fini(&service);
		chiaki_discovery_service_fini(&service_ipv6);
		qDeleteAll(manual_services);
		manual_services.clear();

		hosts = {};
		emit HostsUpdated();
	}

}

void DiscoveryManager::SetSettings(Settings *settings)
{
	this->settings = settings;
	connect(settings, &Settings::ManualHostsUpdated, this, &DiscoveryManager::UpdateManualServices);
	connect(settings, &Settings::RegisteredHostsUpdated, this, &DiscoveryManager::UpdateManualServices);
	UpdateManualServices();
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
	char *ipv6 = strchr(host.toUtf8().data(), ':');
	ChiakiErrorCode err;
	if(ipv6)
		err = chiaki_discovery_wakeup(&log, service_active ? &service_ipv6.discovery : nullptr, host.toUtf8().constData(), credential, ps5);
	else
		err = chiaki_discovery_wakeup(&log, service_active ? &service.discovery : nullptr, host.toUtf8().constData(), credential, ps5);

	if(err != CHIAKI_ERR_SUCCESS)
		throw Exception(QString("Failed to send Packet: %1").arg(chiaki_error_string(err)));
}

const QList<DiscoveryHost> DiscoveryManager::GetHosts() const
{
	QList<DiscoveryHost> ret = hosts;
	QSet<QString> discovered_hosts;
	for(auto host : std::as_const(ret))
		discovered_hosts.insert(host.host_addr);
	for(auto s : std::as_const(manual_services))
		if(s->discovered && !discovered_hosts.contains(s->discovery_host.host_addr))
			ret.append(s->discovery_host);
	return ret;
}

void DiscoveryManager::DiscoveryServiceHosts(QList<DiscoveryHost> hosts)
{
	this->hosts = std::move(hosts);
	emit HostsUpdated();
}

void DiscoveryManager::UpdateManualServices()
{
	if(!settings || !service_active)
		return;

	QSet<QString> hosts;
	for(auto host : settings->GetManualHosts())
		if(settings->GetRegisteredHostRegistered(host.GetMAC()))
			hosts.insert(host.GetHost());

	const auto keys = manual_services.keys();
	for(auto key : keys)
		if(!hosts.contains(key))
			delete manual_services.take(key);

	for(auto host : std::as_const(hosts))
	{
		if(manual_services.contains(host))
			continue;

		ManualService *s = new ManualService;
		s->manager = this;
		manual_services[host] = s;

		ChiakiDiscoveryServiceOptions options = {};
		options.ping_ms = PING_MS;
		options.ping_initial_ms = PING_MS;
		options.hosts_max = 1;
		options.host_drop_pings = DROP_PINGS;
		options.cb = DiscoveryServiceHostsManualCallback;
		options.cb_user = s;

		QByteArray host_utf8 = host.toUtf8();
		options.send_host = host_utf8.data();
		char *ipv6 = strchr(options.send_host, ':');
		if(ipv6)
		{
			sockaddr_in6 addr = {};
			addr.sin6_family = AF_INET6;
			options.send_addr = reinterpret_cast<sockaddr_in6 *>(&addr);
			options.send_addr_size = sizeof(addr);
		}
		else
		{
			sockaddr_in addr = {};
			addr.sin_family = AF_INET;
			options.send_addr = reinterpret_cast<sockaddr_in6 *>(&addr);
			options.send_addr_size = sizeof(addr);
		}
		chiaki_discovery_service_init(&s->service, &options, &log);
	}
}

class DiscoveryManagerPrivate
{
	public:
		static void DiscoveryServiceHosts(DiscoveryManager *discovery_manager, const QList<DiscoveryHost> &hosts)
		{
			QMetaObject::invokeMethod(discovery_manager, "DiscoveryServiceHosts", Qt::ConnectionType::QueuedConnection, Q_ARG(QList<DiscoveryHost>, hosts));
		}

		static void DiscoveryServiceManualHost(DiscoveryManager *discovery_manager)
		{
			QMetaObject::invokeMethod(discovery_manager, &DiscoveryManager::HostsUpdated);
		}
};

static QList<DiscoveryHost> CreateHostsList(ChiakiDiscoveryHost *hosts, size_t hosts_count)
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

	return hosts_list;
}

static void DiscoveryServiceHostsCallback(ChiakiDiscoveryHost *hosts, size_t hosts_count, void *user)
{
	DiscoveryManagerPrivate::DiscoveryServiceHosts(reinterpret_cast<DiscoveryManager *>(user), CreateHostsList(hosts, hosts_count));
}

static void DiscoveryServiceHostsManualCallback(ChiakiDiscoveryHost *hosts, size_t hosts_count, void *user)
{
	ManualService *s = reinterpret_cast<ManualService *>(user);
	s->discovered = hosts_count;
	if(s->discovered)
		s->discovery_host = CreateHostsList(hosts, hosts_count).at(0);
	DiscoveryManagerPrivate::DiscoveryServiceManualHost(s->manager);
}
