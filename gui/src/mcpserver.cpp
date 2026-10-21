// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <mcpserver.h>

#include <QJsonDocument>
#include <QLoggingCategory>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUuid>
#include <QDateTime>
#include <QElapsedTimer>

Q_LOGGING_CATEGORY(chiakiMcp, "chiaki.mcp", QtInfoMsg);

namespace
{
constexpr qsizetype kMaxHeaderBytes = 64 * 1024;
constexpr qsizetype kMaxBodyBytes = 8 * 1024 * 1024;

// Newest first; the first entry is used when the client asks for something unknown.
const QStringList kProtocolVersions = {
	QStringLiteral("2025-06-18"),
	QStringLiteral("2025-03-26"),
	QStringLiteral("2024-11-05"),
};

enum RpcErrorCode
{
	ParseError = -32700,
	InvalidRequest = -32600,
	MethodNotFound = -32601,
	InvalidParams = -32602,
};

QByteArray StatusText(int status)
{
	switch(status)
	{
		case 200: return "OK";
		case 202: return "Accepted";
		case 204: return "No Content";
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 413: return "Payload Too Large";
		default: return "Error";
	}
}

bool IsLoopbackName(const QString &host)
{
	return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("::1") || host == QLatin1String("[::1]");
}

bool IsLoopbackOrigin(const QByteArray &origin)
{
	if(origin.isEmpty() || origin == "null")
		return true;
	return IsLoopbackName(QUrl(QString::fromUtf8(origin)).host());
}

bool IsLoopbackHostHeader(const QByteArray &host_header)
{
	if(host_header.isEmpty())
		return true;
	// "localhost:9339", "127.0.0.1:9339" or "[::1]:9339"
	return IsLoopbackName(QUrl(QStringLiteral("http://") + QString::fromUtf8(host_header)).host());
}
}

McpServer::McpServer(QObject *parent)
	: QObject(parent)
{
}

McpServer::~McpServer()
{
	Close();
}

void McpServer::AddTool(const Tool &tool)
{
	auto it = tool_index.constFind(tool.name);
	if(it != tool_index.constEnd())
	{
		tools[it.value()] = tool;
		return;
	}
	tool_index.insert(tool.name, tools.size());
	tools.append(tool);
}

void McpServer::AddToolExamples(const QString &tool_name, const QJsonArray &examples)
{
	auto it = tool_index.constFind(tool_name);
	if(it == tool_index.constEnd() || examples.isEmpty())
		return;
	Tool &tool = tools[it.value()];
	QStringList lines;
	for(const QJsonValue &example : examples)
		lines << QString::fromUtf8(QJsonDocument(example.toObject()).toJson(QJsonDocument::Compact));
	tool.description += QStringLiteral("\nExamples:\n") + lines.join(QLatin1Char('\n'));
	tool.input_schema["examples"] = examples;
}

void McpServer::AddPrompt(const Prompt &prompt)
{
	for(Prompt &existing : prompts)
	{
		if(existing.name == prompt.name)
		{
			existing = prompt;
			return;
		}
	}
	prompts.append(prompt);
}

void McpServer::SetServerInfo(const QString &name, const QString &version)
{
	server_name = name;
	server_version = version;
}

bool McpServer::Listen(const QHostAddress &address, quint16 port, QString *error)
{
	Close();
	tcp_server = new QTcpServer(this);
	connect(tcp_server, &QTcpServer::newConnection, this, &McpServer::OnNewConnection);
	if(!tcp_server->listen(address, port))
	{
		if(error)
			*error = tcp_server->errorString();
		qCWarning(chiakiMcp) << "Failed to listen on" << address.toString() << port << ":" << tcp_server->errorString();
		delete tcp_server;
		tcp_server = nullptr;
		return false;
	}
	session_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	qCInfo(chiakiMcp) << "MCP server listening on" << Endpoint();
	return true;
}

void McpServer::Close()
{
	const auto sockets = connections.keys();
	connections.clear();
	for(QTcpSocket *socket : sockets)
	{
		socket->disconnect(this);
		socket->abort();
		socket->deleteLater();
	}
	if(tcp_server)
	{
		tcp_server->close();
		tcp_server->deleteLater();
		tcp_server = nullptr;
	}
}

bool McpServer::IsListening() const
{
	return tcp_server && tcp_server->isListening();
}

quint16 McpServer::Port() const
{
	return tcp_server ? tcp_server->serverPort() : 0;
}

QString McpServer::Endpoint() const
{
	if(!tcp_server)
		return QString();
	QString host = tcp_server->serverAddress().toString();
	if(tcp_server->serverAddress().protocol() == QAbstractSocket::IPv6Protocol)
		host = QStringLiteral("[%1]").arg(host);
	return QStringLiteral("http://%1:%2/mcp").arg(host).arg(tcp_server->serverPort());
}

QJsonObject McpServer::TextContent(const QString &text)
{
	return QJsonObject{{"type", "text"}, {"text", text}};
}

QJsonObject McpServer::ImageContent(const QByteArray &data, const QString &mime_type)
{
	return QJsonObject{
		{"type", "image"},
		{"data", QString::fromLatin1(data.toBase64())},
		{"mimeType", mime_type},
	};
}

QJsonObject McpServer::TextResult(const QString &text, bool is_error)
{
	QJsonObject result{{"content", QJsonArray{TextContent(text)}}};
	if(is_error)
		result["isError"] = true;
	return result;
}

QJsonObject McpServer::RpcResult(const QJsonValue &id, const QJsonValue &result)
{
	return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

QJsonObject McpServer::RpcError(const QJsonValue &id, int code, const QString &message)
{
	return QJsonObject{
		{"jsonrpc", "2.0"},
		{"id", id.isUndefined() ? QJsonValue(QJsonValue::Null) : id},
		{"error", QJsonObject{{"code", code}, {"message", message}}},
	};
}

void McpServer::OnNewConnection()
{
	while(tcp_server && tcp_server->hasPendingConnections())
	{
		QTcpSocket *socket = tcp_server->nextPendingConnection();
		connections.insert(socket, Connection());
		connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { OnReadyRead(socket); });
		connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
			connections.remove(socket);
			socket->deleteLater();
		});
	}
}

void McpServer::OnReadyRead(QTcpSocket *socket)
{
	auto it = connections.find(socket);
	if(it == connections.end())
		return;
	it->buffer.append(socket->readAll());
	ProcessBuffer(socket);
}

void McpServer::ProcessBuffer(QTcpSocket *socket)
{
	auto it = connections.find(socket);
	if(it == connections.end() || it->busy)
		return;

	QByteArray &buffer = it->buffer;
	const qsizetype header_end = buffer.indexOf("\r\n\r\n");
	if(header_end < 0)
	{
		if(buffer.size() > kMaxHeaderBytes)
		{
			SendHttp(socket, 413, "Request headers too large", "text/plain", false);
			socket->disconnectFromHost();
		}
		return;
	}

	const QList<QByteArray> lines = buffer.left(header_end).split('\n');
	const QList<QByteArray> request_line = lines.value(0).trimmed().split(' ');
	if(request_line.size() < 3)
	{
		SendHttp(socket, 400, "Malformed request line", "text/plain", false);
		socket->disconnectFromHost();
		return;
	}

	QHash<QByteArray, QByteArray> headers;
	for(qsizetype i = 1; i < lines.size(); i++)
	{
		const QByteArray &line = lines[i];
		const qsizetype colon = line.indexOf(':');
		if(colon <= 0)
			continue;
		headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
	}

	bool length_ok = true;
	const qsizetype content_length = headers.contains("content-length") ? headers.value("content-length").toLongLong(&length_ok) : 0;
	if(!length_ok || content_length < 0 || content_length > kMaxBodyBytes)
	{
		SendHttp(socket, 413, "Invalid or too large Content-Length", "text/plain", false);
		socket->disconnectFromHost();
		return;
	}
	if(headers.value("transfer-encoding").toLower().contains("chunked"))
	{
		SendHttp(socket, 400, "Chunked request bodies are not supported", "text/plain", false);
		socket->disconnectFromHost();
		return;
	}

	const qsizetype body_start = header_end + 4;
	if(buffer.size() - body_start < content_length)
		return; // wait for the rest of the body

	const QByteArray method = request_line[0].toUpper();
	const QByteArray target = request_line[1];
	const QByteArray version = request_line[2].toUpper();
	const QByteArray body = buffer.mid(body_start, content_length);
	buffer.remove(0, body_start + content_length);

	const QByteArray connection_header = headers.value("connection").toLower();
	const bool keep_alive = version == "HTTP/1.0" ? connection_header.contains("keep-alive") : !connection_header.contains("close");

	QByteArray path = target;
	const qsizetype query = path.indexOf('?');
	if(query >= 0)
		path.truncate(query);

	it->busy = true;
	HandleHttpRequest(socket, method, path, headers, body, keep_alive);
}

void McpServer::HandleHttpRequest(QTcpSocket *socket, const QByteArray &method, const QByteArray &path,
		const QHash<QByteArray, QByteArray> &headers, const QByteArray &body, bool keep_alive)
{
	if(path != "/mcp" && path != "/mcp/" && path != "/")
	{
		SendHttp(socket, 404, "Not found. The MCP endpoint is /mcp", "text/plain", keep_alive);
		return;
	}

	// Protect against DNS rebinding: a web page must not be able to drive the app.
	if(!IsLoopbackOrigin(headers.value("origin")) ||
			(tcp_server && tcp_server->serverAddress().isLoopback() && !IsLoopbackHostHeader(headers.value("host"))))
	{
		SendHttp(socket, 403, "Origin not allowed", "text/plain", keep_alive);
		return;
	}

	if(!auth_token.isEmpty())
	{
		const QByteArray expected = "Bearer " + auth_token.toUtf8();
		if(headers.value("authorization") != expected)
		{
			SendHttp(socket, 401, "Missing or invalid bearer token", "text/plain", keep_alive,
					{{"WWW-Authenticate", "Bearer"}});
			return;
		}
	}

	if(method == "GET")
	{
		// No server initiated SSE stream is offered.
		SendHttp(socket, 405, "Server-sent event streams are not supported; use POST", "text/plain", keep_alive,
				{{"Allow", "POST, DELETE"}});
		return;
	}
	if(method == "DELETE")
	{
		SendHttp(socket, 200, QByteArray(), "text/plain", keep_alive);
		return;
	}
	if(method != "POST")
	{
		SendHttp(socket, 405, "Method not allowed", "text/plain", keep_alive, {{"Allow", "POST, DELETE"}});
		return;
	}

	QJsonParseError parse_error;
	const QJsonDocument doc = QJsonDocument::fromJson(body, &parse_error);
	if(parse_error.error != QJsonParseError::NoError || (!doc.isObject() && !doc.isArray()))
	{
		const QJsonObject error = RpcError(QJsonValue::Null, ParseError, QStringLiteral("Parse error: %1").arg(parse_error.errorString()));
		SendHttp(socket, 400, QJsonDocument(error).toJson(QJsonDocument::Compact), "application/json", keep_alive);
		return;
	}

	const QJsonValue message = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
	QPointer<QTcpSocket> guarded_socket(socket);
	HandleMessage(message, [this, guarded_socket, keep_alive](const QJsonValue &response) {
		if(!guarded_socket || !connections.contains(guarded_socket.data()))
			return;
		const QList<QPair<QByteArray, QByteArray>> extra = {{"Mcp-Session-Id", session_id.toUtf8()}};
		if(response.isUndefined() || response.isNull())
		{
			SendHttp(guarded_socket, 202, QByteArray(), "application/json", keep_alive, extra);
			return;
		}
		const QByteArray payload = response.isArray()
			? QJsonDocument(response.toArray()).toJson(QJsonDocument::Compact)
			: QJsonDocument(response.toObject()).toJson(QJsonDocument::Compact);
		SendHttp(guarded_socket, 200, payload, "application/json", keep_alive, extra);
	});
}

void McpServer::SendHttp(QTcpSocket *socket, int status, const QByteArray &body, const QByteArray &content_type,
		bool keep_alive, const QList<QPair<QByteArray, QByteArray>> &extra_headers)
{
	QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + StatusText(status) + "\r\n";
	if(!body.isEmpty())
		response += "Content-Type: " + content_type + "\r\n";
	response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
	response += "Cache-Control: no-store\r\n";
	response += keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
	for(const auto &header : extra_headers)
		response += header.first + ": " + header.second + "\r\n";
	response += "\r\n";
	response += body;
	socket->write(response);

	auto it = connections.find(socket);
	if(it != connections.end())
		it->busy = false;
	if(!keep_alive)
	{
		socket->disconnectFromHost();
		return;
	}
	// Continue with any pipelined request.
	QMetaObject::invokeMethod(this, [this, guarded = QPointer<QTcpSocket>(socket)]() {
		if(guarded)
			ProcessBuffer(guarded);
	}, Qt::QueuedConnection);
}

void McpServer::HandleMessage(const QJsonValue &message, const std::function<void(const QJsonValue &response)> &done)
{
	if(message.isObject())
	{
		HandleRequest(message.toObject(), done);
		return;
	}

	const QJsonArray batch = message.toArray();
	if(batch.isEmpty())
	{
		done(RpcError(QJsonValue::Null, InvalidRequest, QStringLiteral("Empty batch")));
		return;
	}

	struct BatchState
	{
		QVector<QJsonValue> responses;
		int remaining = 0;
	};
	auto state = QSharedPointer<BatchState>::create();
	state->responses.resize(batch.size());
	state->remaining = batch.size();
	for(int i = 0; i < batch.size(); i++)
	{
		auto finish_one = [state, i, done](const QJsonValue &response) {
			state->responses[i] = response;
			if(--state->remaining > 0)
				return;
			QJsonArray out;
			for(const QJsonValue &r : state->responses)
				if(!r.isUndefined() && !r.isNull())
					out.append(r);
			done(out.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(out));
		};
		if(!batch[i].isObject())
			finish_one(RpcError(QJsonValue::Null, InvalidRequest, QStringLiteral("Batch entries must be objects")));
		else
			HandleRequest(batch[i].toObject(), finish_one);
	}
}

void McpServer::HandleRequest(const QJsonObject &request, const std::function<void(const QJsonValue &response)> &done)
{
	const QJsonValue id = request.value("id");
	const QString method = request.value("method").toString();
	const bool is_notification = id.isUndefined();

	if(method.isEmpty())
	{
		// Responses to server initiated requests (we send none) or garbage.
		done(is_notification ? QJsonValue(QJsonValue::Undefined) : QJsonValue(RpcError(id, InvalidRequest, QStringLiteral("Missing method"))));
		return;
	}
	if(is_notification)
	{
		// notifications/initialized, notifications/cancelled, ... need no answer.
		done(QJsonValue::Undefined);
		return;
	}

	const QJsonObject params = request.value("params").toObject();

	if(method == QLatin1String("initialize"))
	{
		done(RpcResult(id, InitializeResult(params)));
		return;
	}
	if(method == QLatin1String("ping"))
	{
		done(RpcResult(id, QJsonObject()));
		return;
	}
	if(method == QLatin1String("tools/list"))
	{
		done(RpcResult(id, QJsonObject{{"tools", ToolList()}}));
		return;
	}
	if(method == QLatin1String("resources/list"))
	{
		done(RpcResult(id, QJsonObject{{"resources", QJsonArray()}}));
		return;
	}
	if(method == QLatin1String("resources/templates/list"))
	{
		done(RpcResult(id, QJsonObject{{"resourceTemplates", QJsonArray()}}));
		return;
	}
	if(method == QLatin1String("prompts/list"))
	{
		done(RpcResult(id, QJsonObject{{"prompts", PromptList()}}));
		return;
	}
	if(method == QLatin1String("prompts/get"))
	{
		const QString name = params.value("name").toString();
		for(const Prompt &prompt : prompts)
		{
			if(prompt.name != name)
				continue;
			const QJsonObject args = params.value("arguments").toObject();
			for(const PromptArgument &arg : prompt.arguments)
			{
				if(arg.required && args.value(arg.name).toString().isEmpty())
				{
					done(RpcError(id, InvalidParams, QStringLiteral("Missing required argument: %1").arg(arg.name)));
					return;
				}
			}
			const QJsonObject message{
				{"role", "user"},
				{"content", TextContent(prompt.render(args))},
			};
			done(RpcResult(id, QJsonObject{{"description", prompt.description}, {"messages", QJsonArray{message}}}));
			return;
		}
		done(RpcError(id, InvalidParams, QStringLiteral("Unknown prompt: %1").arg(name)));
		return;
	}
	if(method == QLatin1String("logging/setLevel"))
	{
		done(RpcResult(id, QJsonObject()));
		return;
	}
	if(method == QLatin1String("tools/call"))
	{
		const QString name = params.value("name").toString();
		auto it = tool_index.constFind(name);
		if(it == tool_index.constEnd())
		{
			QStringList names;
			for(const Tool &tool : tools)
				names << tool.name;
			done(RpcError(id, InvalidParams, QStringLiteral("Unknown tool: %1. Available tools: %2").arg(name, names.join(QStringLiteral(", ")))));
			return;
		}
		const QJsonValue arguments = params.value("arguments");
		if(!arguments.isUndefined() && !arguments.isNull() && !arguments.isObject())
		{
			done(RpcError(id, InvalidParams, QStringLiteral("Tool arguments must be an object")));
			return;
		}
		emit ToolCalled(name);
		// Copy the handler: a tool may re-register tools while running.
		const Handler handler = tools[it.value()].handler;
		auto answered = QSharedPointer<bool>::create(false);
		auto started = QSharedPointer<QElapsedTimer>::create();
		started->start();
		const qint64 started_at = QDateTime::currentMSecsSinceEpoch();
		QPointer<McpServer> self(this);
		handler(arguments.toObject(), [self, id, done, answered, started, started_at, name, arguments](const QJsonObject &result) {
			if(*answered)
				return;
			*answered = true;
			if(self)
			{
				// Keep the first text block as the outcome; images are summarized.
				QString outcome;
				int images = 0;
				for(const QJsonValue &c : result.value("content").toArray())
				{
					const QJsonObject block = c.toObject();
					if(block.value("type").toString() == QLatin1String("image"))
						images++;
					else if(outcome.isEmpty())
						outcome = block.value("text").toString().simplified();
				}
				if(outcome.size() > 240)
					outcome = outcome.left(237) + QStringLiteral("...");
				if(images)
					outcome += QStringLiteral(" [+%1 screenshot]").arg(images);
				// Never keep typed text in the history: it may be a password or PIN.
				QJsonObject recorded_args = arguments.toObject();
				for(const char *secret : {"text", "pin", "password", "token"})
				{
					if(name == QLatin1String("type_text") || qstrcmp(secret, "text") != 0)
					{
						const QJsonValue value = recorded_args.value(QLatin1String(secret));
						if(value.isString())
							recorded_args[QLatin1String(secret)] = QStringLiteral("<%1 characters hidden>").arg(value.toString().size());
					}
				}
				self->history.append({started_at, name, recorded_args, outcome,
						result.value("isError").toBool(false), started->elapsed()});
				while(self->history.size() > 100)
					self->history.removeFirst();
			}
			// Mirror a JSON text result as structuredContent (MCP 2025-06-18) so clients can use the data
			// without parsing text. structuredContent must be an object, so arrays are wrapped.
			QJsonObject final_result = result;
			if(!final_result.contains("structuredContent"))
			{
				for(const QJsonValue &c : final_result.value("content").toArray())
				{
					const QJsonObject block = c.toObject();
					if(block.value("type").toString() != QLatin1String("text"))
						continue;
					const QString text = block.value("text").toString().trimmed();
					if(text.startsWith(QLatin1Char('{')) || text.startsWith(QLatin1Char('[')))
					{
						QJsonParseError error;
						const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
						if(error.error == QJsonParseError::NoError)
							final_result["structuredContent"] = doc.isObject() ? doc.object() : QJsonObject{{"items", doc.array()}};
					}
					break;
				}
			}
			done(RpcResult(id, final_result));
		});
		return;
	}

	done(RpcError(id, MethodNotFound, QStringLiteral("Method not found: %1").arg(method)));
}

QJsonObject McpServer::InitializeResult(const QJsonObject &params)
{
	QString version = params.value("protocolVersion").toString();
	if(!kProtocolVersions.contains(version))
		version = kProtocolVersions.first();

	QJsonObject server_info{{"name", server_name}};
	if(!server_version.isEmpty())
		server_info["version"] = server_version;

	QJsonObject result{
		{"protocolVersion", version},
		{"capabilities", QJsonObject{
			{"tools", QJsonObject{{"listChanged", false}}},
			{"prompts", QJsonObject{{"listChanged", false}}},
		}},
		{"serverInfo", server_info},
	};
	if(!instructions.isEmpty())
		result["instructions"] = instructions;
	return result;
}

QJsonArray McpServer::PromptList() const
{
	QJsonArray list;
	for(const Prompt &prompt : prompts)
	{
		QJsonArray arguments;
		for(const PromptArgument &arg : prompt.arguments)
			arguments.append(QJsonObject{{"name", arg.name}, {"description", arg.description}, {"required", arg.required}});
		QJsonObject entry{{"name", prompt.name}, {"description", prompt.description}, {"arguments", arguments}};
		if(!prompt.title.isEmpty())
			entry["title"] = prompt.title;
		list.append(entry);
	}
	return list;
}

QJsonArray McpServer::ToolList() const
{
	QJsonArray list;
	for(const Tool &tool : tools)
	{
		QJsonObject schema = tool.input_schema;
		if(schema.isEmpty())
			schema = QJsonObject{{"type", "object"}, {"properties", QJsonObject()}};
		QJsonObject entry{
			{"name", tool.name},
			{"description", tool.description},
			{"inputSchema", schema},
		};
		QJsonObject annotations{{"readOnlyHint", tool.read_only}};
		if(!tool.title.isEmpty())
		{
			entry["title"] = tool.title;
			annotations["title"] = tool.title;
		}
		entry["annotations"] = annotations;
		list.append(entry);
	}
	return list;
}
