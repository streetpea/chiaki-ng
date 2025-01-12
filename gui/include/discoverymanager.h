// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_DISCOVERYMANAGER_H
#define CHIAKI_DISCOVERYMANAGER_H

#include <chiaki/discoveryservice.h>

#include "host.h"

#include <QObject>
#include <QList>
#include <QHash>

struct DiscoveryHost
{
	bool ps5;
	ChiakiDiscoveryHostState state;
	ChiakiTarget target;
	uint16_t host_request_port;
#define STRING_MEMBER(name) QString name;
	CHIAKI_DISCOVERY_HOST_STRING_FOREACH(STRING_MEMBER)
#undef STRING_MEMBER

	HostMAC GetHostMAC() const;
};

Q_DECLARE_METATYPE(DiscoveryHost)

struct ManualService
{
	~ManualService() { chiaki_discovery_service_fini(&service); }

	class DiscoveryManager *manager;
	bool discovered = false;
	DiscoveryHost discovery_host;
	ChiakiDiscoveryService service;
};

class Settings;

class DiscoveryManager : public QObject
{
	Q_OBJECT

	friend class DiscoveryManagerPrivate;

	private:
		ChiakiLog log;
		QList<ChiakiDiscoveryService> services;
		ChiakiDiscoveryService service;
		ChiakiDiscoveryService service_ipv6;
		bool service_active;
		bool service_active_ipv6;
		QList<DiscoveryHost> hosts;
		Settings *settings = {};
		QHash<QString, ManualService*> manual_services;

	private slots:
		void DiscoveryServiceHosts(QList<DiscoveryHost> hosts);
		void UpdateManualServices();

	public:
		explicit DiscoveryManager(QObject *parent = nullptr);
		~DiscoveryManager();

		void SetActive(bool active);
		void SetSettings(Settings *settings);

		void SendWakeup(const QString &host, const QByteArray &regist_key, bool ps5);

		bool GetActive() const { return service_active; }
		const QList<DiscoveryHost> GetHosts() const;

	signals:
		void HostsUpdated();
};

#endif //CHIAKI_DISCOVERYMANAGER_H
