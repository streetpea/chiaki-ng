// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_MCPSERVER_H
#define CHIAKI_MCPSERVER_H

#include <QObject>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QPointer>
#include <QString>

#include <functional>

Q_DECLARE_LOGGING_CATEGORY(chiakiMcp);

class QTcpServer;
class QTcpSocket;

/**
 * Minimal Model Context Protocol server using the "Streamable HTTP" transport
 * (JSON-RPC 2.0 over HTTP POST, plain JSON responses, no SSE stream).
 *
 * Lives on the GUI thread. Tool handlers may answer synchronously or later
 * (e.g. after the render thread captured a frame) by calling the responder.
 */
class McpServer : public QObject
{
	Q_OBJECT

	public:
		using Responder = std::function<void(const QJsonObject &result)>;
		using Handler = std::function<void(const QJsonObject &args, const Responder &respond)>;

		struct Tool
		{
			QString name;
			QString title;
			QString description;
			QJsonObject input_schema;
			bool read_only = false;
			Handler handler;
		};

		explicit McpServer(QObject *parent = nullptr);
		~McpServer() override;

		void AddTool(const Tool &tool);
		QList<Tool> Tools() const { return tools; }

		struct PromptArgument
		{
			QString name;
			QString description;
			bool required = false;
		};
		struct Prompt
		{
			QString name;
			QString title;
			QString description;
			QList<PromptArgument> arguments;
			// Returns the text of the single user message the prompt expands to.
			std::function<QString(const QJsonObject &args)> render;
		};
		void AddPrompt(const Prompt &prompt);
		// Appends "Examples:" to a tool's description and sets inputSchema.examples.
		void AddToolExamples(const QString &tool_name, const QJsonArray &examples);

		// Recent tool calls of this server run, oldest first (kept for get_action_history).
		struct HistoryEntry
		{
			qint64 timestamp_ms;
			QString tool;
			QJsonObject arguments;
			QString outcome;
			bool is_error;
			qint64 duration_ms;
		};
		QList<HistoryEntry> History() const { return history; }

		void SetServerInfo(const QString &name, const QString &version);
		void SetInstructions(const QString &instructions) { this->instructions = instructions; }
		// Empty token disables authentication (the server only listens on loopback by default).
		void SetAuthToken(const QString &token) { auth_token = token; }

		bool Listen(const QHostAddress &address, quint16 port, QString *error = nullptr);
		void Close();
		bool IsListening() const;
		quint16 Port() const;
		QString Endpoint() const;

		static QJsonObject TextResult(const QString &text, bool is_error = false);
		static QJsonObject ErrorResult(const QString &text) { return TextResult(text, true); }
		static QJsonObject ImageContent(const QByteArray &data, const QString &mime_type);
		static QJsonObject TextContent(const QString &text);

		// Handles one JSON-RPC message (object or batch array). Calls done with the
		// response document, or with an empty value if nothing must be sent back.
		void HandleMessage(const QJsonValue &message, const std::function<void(const QJsonValue &response)> &done);

	signals:
		void ToolCalled(const QString &name);

	private:
		struct Connection
		{
			QByteArray buffer;
			bool busy = false;
		};

		void OnNewConnection();
		void OnReadyRead(QTcpSocket *socket);
		void ProcessBuffer(QTcpSocket *socket);
		void HandleHttpRequest(QTcpSocket *socket, const QByteArray &method, const QByteArray &path,
				const QHash<QByteArray, QByteArray> &headers, const QByteArray &body, bool keep_alive);
		void SendHttp(QTcpSocket *socket, int status, const QByteArray &body, const QByteArray &content_type,
				bool keep_alive, const QList<QPair<QByteArray, QByteArray>> &extra_headers = {});

		void HandleRequest(const QJsonObject &request, const std::function<void(const QJsonValue &response)> &done);
		QJsonObject InitializeResult(const QJsonObject &params);
		QJsonArray ToolList() const;
		QJsonArray PromptList() const;

		static QJsonObject RpcResult(const QJsonValue &id, const QJsonValue &result);
		static QJsonObject RpcError(const QJsonValue &id, int code, const QString &message);

		QTcpServer *tcp_server = nullptr;
		QHash<QTcpSocket *, Connection> connections;
		QList<Tool> tools;
		QList<Prompt> prompts;
		QList<HistoryEntry> history;
		QHash<QString, int> tool_index;
		QString server_name = QStringLiteral("chiaki-ng");
		QString server_version;
		QString instructions;
		QString auth_token;
		QString session_id;
};

#endif // CHIAKI_MCPSERVER_H
