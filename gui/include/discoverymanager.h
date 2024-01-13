// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_DISCOVERYMANAGER_H
#define CHIAKI_DISCOVERYMANAGER_H

#include <chiaki/discoveryservice.h>

#include "host.h"

#include <QObject>
#include <QList>

struct DiscoveryHost
{
	bool ps5;
	ChiakiDiscoveryHostState state;
	uint16_t host_request_port;
#define STRING_MEMBER(name) QString name;
	CHIAKI_DISCOVERY_HOST_STRING_FOREACH(STRING_MEMBER)
#undef STRING_MEMBER

	HostMAC GetHostMAC() const;
};

Q_DECLARE_METATYPE(DiscoveryHost)

class DiscoveryManager : public QObject
{
	Q_OBJECT

	friend class DiscoveryManagerPrivate;

	private:
		ChiakiLog log;
		ChiakiDiscoveryService service;
		bool service_active;
		bool queueResponses;
		QList<DiscoveryHost> hosts;



	private slots:
		void DiscoveryServiceHosts(QList<DiscoveryHost> hosts);

	public:
		ChiakiDiscoveryHostState last_state;

		explicit DiscoveryManager(QObject *parent = nullptr, bool queueResponses = true);
		~DiscoveryManager();

		void SetActive(bool active);

		void SendWakeup(const QString &host, const QByteArray &regist_key, bool ps5);

		const QList<DiscoveryHost> GetHosts() const { return hosts; }
		void discoverHostState(QString addr);

	signals:
		void HostsUpdated();
};

#endif //CHIAKI_DISCOVERYMANAGER_H
