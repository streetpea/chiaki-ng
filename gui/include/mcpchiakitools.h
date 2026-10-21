// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_MCPCHIAKITOOLS_H
#define CHIAKI_MCPCHIAKITOOLS_H

#include "mcpserver.h"

#include <QObject>
#include <QPointer>

#include <chiaki/controller.h>

class McpUiTools;
class QmlMainWindow;

struct McpLaunchOptions
{
	bool enable = false;      // --mcp or CHIAKI_MCP=1
	int port = -1;            // --mcp-port or CHIAKI_MCP_PORT, -1 = from settings
	QString token;            // --mcp-token or CHIAKI_MCP_TOKEN
	QString bind_address;     // CHIAKI_MCP_BIND, default 127.0.0.1
};

/**
 * Exposes chiaki-ng to AI agents over MCP: the generic screenshot / UI / input
 * tools plus app specific ones (consoles, stream session, a virtual controller).
 */
class McpChiakiTools : public QObject
{
	Q_OBJECT

	public:
		static constexpr quint16 kDefaultPort = 9339;

		McpChiakiTools(QmlMainWindow *window, const McpLaunchOptions &options, QObject *parent = nullptr);
		~McpChiakiTools() override;

		static McpLaunchOptions LaunchOptionsFromEnvironment();

		bool Start(QString *error = nullptr);
		void Stop();
		McpServer *Server() { return server; }

	private:
		void RegisterTools();
		void RegisterPrompts();
		void AddExamples();
		void ApplySettings();
		QJsonObject AppState() const;

		void RunControllerSteps(const QJsonArray &steps, int index, const McpServer::Responder &respond);
		bool StateFromStep(const QJsonObject &step, ChiakiControllerState *state, QString *error) const;
		void ReleaseController();

		QPointer<QmlMainWindow> window;
		McpLaunchOptions options;
		McpServer *server = nullptr;
		McpUiTools *ui_tools = nullptr;
		quint64 controller_generation = 0;
};

#endif // CHIAKI_MCPCHIAKITOOLS_H
