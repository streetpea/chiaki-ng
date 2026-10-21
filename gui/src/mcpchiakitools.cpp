// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <mcpchiakitools.h>
#include <mcpuitools.h>
#include <qmlbackend.h>
#include <qmlmainwindow.h>
#include <qmlsettings.h>
#include <streamsession.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QMetaEnum>
#include <QSharedPointer>
#include <QTimer>

namespace
{
constexpr int kMaxControllerSequenceMs = 60000;

const char *const kInstructions =
	"You are controlling chiaki-ng, a PlayStation Remote Play client. Call get_usage_guide once for the full playbook.\n"
	"Workflow: take_screenshot to see the app (full window, native resolution, nothing cropped), then act, "
	"then screenshot again to confirm. Coordinates for click/scroll/drag are pixels of the latest screenshot.\n"
	"App UI (console list, settings, dialogs): use get_ui_tree / find_ui_elements to locate controls, then "
	"click_element, or focus_element + press_key (Up/Down/Left/Right move focus, Return activates, Escape goes back). "
	"list_hosts / connect_host / wake_host / get_app_state cover consoles directly.\n"
	"During a stream the picture is the PlayStation screen: drive the console with controller_input "
	"(cross = confirm, circle = back, dpad to navigate, ps = home, options, touchpad...). "
	"press_key ctrl+o opens the chiaki stream menu; disconnect_stream ends the session. "
	"Do not guess sleep times: wait_for (element appears/disappears, screen_changed, screen_stable) and "
	"wait_for_stream return as soon as the app or console is ready.";

const char *const kUsageGuide =
	"# Using chiaki-ng through MCP\n"
	"\n"
	"## The loop\n"
	"1. Look: take_screenshot (annotate=true numbers every control and lists its id). The image is the complete app "
	"at native resolution, every window included, never cropped. For chiaki's own menus read_screen_text is a cheap "
	"alternative, and get_app_state.location says which view/dialog/tab is open.\n"
	"2. Act: one action at a time. Pass screenshot_after=true to get the resulting screen in the same reply, and "
	"expect={until: element|element_gone|screen_changed|screen_stable, text: ...} to have the action verified in the "
	"same call (it fails loudly with EXPECTATION NOT MET when nothing happened). Example: controller_input "
	"{buttons:[\"cross\"], expect:{until:\"screen_changed\"}}.\n"
	"3. Verify: confirm the screen changed as expected before the next step. If not, take a fresh screenshot and re-plan.\n"
	"\n"
	"Small text unreadable (large/4K window scaled to fit)? take_screenshot {region: {x, y, width, height}} returns that "
	"part at native resolution; keep clicking in full-screenshot coordinates.\n"
	"\n"
	"## Two worlds\n"
	"- chiaki-ng UI (console list, settings, dialogs, stream menu): Qt controls. Prefer click_element with an id from "
	"get_ui_tree / find_ui_elements / annotate marks. Keyboard works everywhere: arrows move the focus (FOCUSED in the tree), "
	"Return/Space activate, Escape closes or goes back, Tab cycles. Big screens like Settings have hundreds of "
	"controls: use get_ui_tree {interactive_only: true} or {root: <id of the dialog/page>} to keep the output small.\n"
	"- The PlayStation (while streaming): only pixels. Read the screenshot and use controller_input. "
	"get_ui_tree only describes chiaki's own overlay there, not the console UI.\n"
	"\n"
	"## Starting a stream\n"
	"list_hosts -> connect_host {index} -> wait_for_stream {state: video}. state 'standby' consoles are woken first, "
	"which can take ~20 s. Unregistered consoles open a registration dialog that needs the user's PSN account and a PIN "
	"shown on the console: ask the user, never guess. A login passcode dialog may appear; ask the user for it.\n"
	"\n"
	"## Controller cheat sheet (PS4/PS5)\n"
	"- cross = confirm/select, circle = back/cancel, triangle/square = context actions, options = game/app menu.\n"
	"- dpad moves the selection highlight in system menus; left_stick does too but dpad is more precise.\n"
	"- ps = home screen / control center (PS5: a short press opens the control center, hold_ms 1000 goes home).\n"
	"- share/create = capture menu, touchpad = click the touchpad; touch {x,y} 0..1 for touch positions.\n"
	"- Several presses: sequence [\"dpad_right\", \"dpad_right\", \"cross\"]. Combos or held sticks: steps with "
	"buttons + left_stick {x,y} (-1..1, y down positive) + hold_ms.\n"
	"- After an input, wait_for {until: screen_changed} confirms the console reacted; wait_for {until: screen_stable} "
	"waits out loading and menu animations (it times out on live game footage, which never settles).\n"
	"- Inputs need ~80-150 ms to register reliably (default hold_ms 100); console menus animate, so wait 300-800 ms "
	"(screenshot_delay_ms) before judging the result. Games and apps can take 5-30 s to start.\n"
	"- Text fields on the console open the on-screen keyboard: navigate it with dpad + cross.\n"
	"\n"
	"## chiaki-ng shortcuts (press_key)\n"
	"ctrl+o stream menu, ctrl+s stretch, ctrl+z zoom, ctrl+m mute, F11 fullscreen, ctrl+q quit (avoid unless asked). "
	"While streaming with the menu closed, plain keys and mouse go to the console as mapped controller input.\n"
	"\n"
	"## When something looks wrong\n"
	"- Run mcp_self_check: it tests window, capture, UI access, focus, consoles and stream and suggests fixes.\n"
	"- Black or frozen picture: get_app_state (connected, has_video, bitrate, packet loss); the console may be loading or asleep.\n"
	"- Screenshot fails: the window may be minimized; window_control {action: restore}.\n"
	"- Element id not found: the UI changed; refresh with get_ui_tree (the error lists likely matches).\n"
	"- Lost track during a long task: get_action_history shows what was already tried and what failed.\n"
	"- Clicks land in the wrong place: coordinates are pixels of the LAST screenshot; take a new one after resizing.\n"
	"- Leave the console as you found it when done (disconnect_stream only if the user wants).\n";

QJsonObject Schema(const QJsonObject &properties, const QStringList &required = {})
{
	QJsonObject schema{{"type", "object"}, {"properties", properties}, {"additionalProperties", false}};
	if(!required.isEmpty())
		schema["required"] = QJsonArray::fromStringList(required);
	return schema;
}

QJsonObject Prop(const QString &type, const QString &description)
{
	return QJsonObject{{"type", type}, {"description", description}};
}

QString ToJson(const QJsonValue &value)
{
	return value.isArray()
		? QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented))
		: QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented));
}

const QHash<QString, uint32_t> &ButtonNames()
{
	static const QHash<QString, uint32_t> names = {
		{"cross", CHIAKI_CONTROLLER_BUTTON_CROSS}, {"x", CHIAKI_CONTROLLER_BUTTON_CROSS},
		{"circle", CHIAKI_CONTROLLER_BUTTON_MOON}, {"o", CHIAKI_CONTROLLER_BUTTON_MOON},
		{"square", CHIAKI_CONTROLLER_BUTTON_BOX},
		{"triangle", CHIAKI_CONTROLLER_BUTTON_PYRAMID},
		{"dpad_left", CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT}, {"left", CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT},
		{"dpad_right", CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT}, {"right", CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT},
		{"dpad_up", CHIAKI_CONTROLLER_BUTTON_DPAD_UP}, {"up", CHIAKI_CONTROLLER_BUTTON_DPAD_UP},
		{"dpad_down", CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN}, {"down", CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN},
		{"l1", CHIAKI_CONTROLLER_BUTTON_L1}, {"r1", CHIAKI_CONTROLLER_BUTTON_R1},
		{"l3", CHIAKI_CONTROLLER_BUTTON_L3}, {"r3", CHIAKI_CONTROLLER_BUTTON_R3},
		{"options", CHIAKI_CONTROLLER_BUTTON_OPTIONS}, {"start", CHIAKI_CONTROLLER_BUTTON_OPTIONS},
		{"share", CHIAKI_CONTROLLER_BUTTON_SHARE}, {"create", CHIAKI_CONTROLLER_BUTTON_SHARE},
		{"touchpad", CHIAKI_CONTROLLER_BUTTON_TOUCHPAD},
		{"ps", CHIAKI_CONTROLLER_BUTTON_PS}, {"home", CHIAKI_CONTROLLER_BUTTON_PS},
	};
	return names;
}

int16_t StickAxis(double v)
{
	return static_cast<int16_t>(qRound(qBound(-1.0, v, 1.0) * 32767.0));
}

template<typename Enum>
QString EnumKey(Enum value)
{
	const char *key = QMetaEnum::fromType<Enum>().valueToKey(static_cast<int>(value));
	return key ? QString::fromLatin1(key) : QString::number(static_cast<int>(value));
}
}

McpChiakiTools::McpChiakiTools(QmlMainWindow *window, const McpLaunchOptions &options, QObject *parent)
	: QObject(parent)
	, window(window)
	, options(options)
{
	server = new McpServer(this);
	server->SetServerInfo(QStringLiteral("chiaki-ng"), QCoreApplication::applicationVersion());
	server->SetInstructions(QString::fromUtf8(kInstructions));

	ui_tools = new McpUiTools(window, window->quickWindow(), this);
	ui_tools->AddExcludedWindow(window->quickWindow());
	QPointer<QmlMainWindow> guarded_window(window);
	ui_tools->SetMainFrameGrabber([guarded_window](const McpUiTools::FrameCallback &callback) {
		if(!guarded_window)
		{
			callback(QImage(), QStringLiteral("Window closed"));
			return;
		}
		guarded_window->requestFrameCapture(callback);
	});

	RegisterTools();
	RegisterPrompts();
	connect(server, &McpServer::ToolCalled, this, [](const QString &name) { qCInfo(chiakiMcp) << "AI tool call:" << name; });

	if(QmlBackend *backend = window->getBackend())
	{
		QmlSettings *settings = backend->qmlSettings();
		connect(settings, &QmlSettings::mcpServerEnabledChanged, this, &McpChiakiTools::ApplySettings);
		connect(settings, &QmlSettings::mcpServerPortChanged, this, &McpChiakiTools::ApplySettings);
		// A stream ending must not leave virtual buttons held for the next one.
		connect(backend, &QmlBackend::sessionChanged, this, [this](StreamSession *) { controller_generation++; });
	}
	ApplySettings();
}

McpChiakiTools::~McpChiakiTools()
{
	Stop();
}

McpLaunchOptions McpChiakiTools::LaunchOptionsFromEnvironment()
{
	McpLaunchOptions o;
	const QString enable = qEnvironmentVariable("CHIAKI_MCP").trimmed().toLower();
	o.enable = enable == QLatin1String("1") || enable == QLatin1String("true") || enable == QLatin1String("on");
	bool ok = false;
	const int port = qEnvironmentVariableIntValue("CHIAKI_MCP_PORT", &ok);
	if(ok && port > 0 && port < 65536)
		o.port = port;
	o.token = qEnvironmentVariable("CHIAKI_MCP_TOKEN");
	o.bind_address = qEnvironmentVariable("CHIAKI_MCP_BIND");
	return o;
}

void McpChiakiTools::ApplySettings()
{
	QmlSettings *settings = window && window->getBackend() ? window->getBackend()->qmlSettings() : nullptr;
	const bool enabled = options.enable || (settings && settings->mcpServerEnabled());
	if(!enabled)
	{
		Stop();
		return;
	}
	const quint16 port = options.port > 0 ? quint16(options.port)
		: (settings && settings->mcpServerPort() > 0 ? quint16(settings->mcpServerPort()) : kDefaultPort);
	if(server->IsListening() && server->Port() == port)
		return;
	QString error;
	if(!Start(&error))
		qCWarning(chiakiMcp) << "MCP server could not start:" << error;
}

bool McpChiakiTools::Start(QString *error)
{
	QmlSettings *settings = window && window->getBackend() ? window->getBackend()->qmlSettings() : nullptr;
	const quint16 port = options.port > 0 ? quint16(options.port)
		: (settings && settings->mcpServerPort() > 0 ? quint16(settings->mcpServerPort()) : kDefaultPort);
	QHostAddress address(QHostAddress::LocalHost);
	if(!options.bind_address.isEmpty() && !address.setAddress(options.bind_address))
	{
		if(error)
			*error = QStringLiteral("Invalid CHIAKI_MCP_BIND address %1").arg(options.bind_address);
		return false;
	}
	if(!address.isLoopback() && options.token.isEmpty())
	{
		// Anyone on the network could otherwise drive the app and the console.
		if(error)
			*error = QStringLiteral("Refusing to listen on a non-loopback address without CHIAKI_MCP_TOKEN");
		return false;
	}
	server->SetAuthToken(options.token);
	return server->Listen(address, port, error);
}

void McpChiakiTools::Stop()
{
	ReleaseController();
	if(server)
		server->Close();
}

void McpChiakiTools::ReleaseController()
{
	controller_generation++;
	if(!window || !window->getBackend())
		return;
	if(StreamSession *session = window->getBackend()->qmlSession())
	{
		ChiakiControllerState idle;
		chiaki_controller_state_set_idle(&idle);
		session->SetMcpControllerState(idle);
	}
}

QJsonObject McpChiakiTools::AppState() const
{
	QJsonObject state;
	if(!window)
		return state;
	QmlBackend *backend = window->getBackend();
	StreamSession *session = backend ? backend->qmlSession() : nullptr;
	state["view"] = session ? QStringLiteral("stream") : QStringLiteral("main");
	if(session)
	{
		state["stream"] = QJsonObject{
			{"host", session->GetHost()},
			{"connected", session->IsConnected()},
			{"connecting", session->IsConnecting()},
			{"has_video", window->hasVideo()},
			{"muted", session->GetMuted()},
			{"measured_bitrate_mbps", session->GetMeasuredBitrate()},
			{"average_packet_loss", session->GetAveragePacketLoss()},
			{"frames_lost", session->GetFramesLost()},
			{"dropped_frames", window->droppedFrames()},
			{"cant_display", session->GetCantDisplay()},
			{"video_mode", EnumKey(window->videoMode())},
		};
	}
	if(backend)
	{
		state["discovery_enabled"] = backend->discoveryEnabled();
		state["psn_connect_state"] = EnumKey(backend->connectState());
		state["auto_connect"] = backend->autoConnect();
		state["consoles"] = backend->hosts().size();
		state["controllers_connected"] = backend->qmlControllers().size();
	}
	state["ui"] = ui_tools->WindowInfo().value("main_window");
	state["location"] = ui_tools->ScreenSummary();
	if(server && server->IsListening())
		state["mcp_endpoint"] = server->Endpoint();
	return state;
}

bool McpChiakiTools::StateFromStep(const QJsonObject &step, ChiakiControllerState *state, QString *error) const
{
	chiaki_controller_state_set_idle(state);
	QJsonArray buttons = step.value("buttons").toArray();
	if(step.value("button").isString())
		buttons.append(step.value("button"));
	for(const QJsonValue &b : buttons)
	{
		const QString name = b.toString().trimmed().toLower();
		if(name == QLatin1String("l2"))
		{
			state->l2_state = 0xff;
			continue;
		}
		if(name == QLatin1String("r2"))
		{
			state->r2_state = 0xff;
			continue;
		}
		auto it = ButtonNames().constFind(name);
		if(it == ButtonNames().constEnd())
		{
			*error = QStringLiteral("Unknown button '%1'. Valid: cross, circle, square, triangle, dpad_up/down/left/right, "
					"l1, r1, l2, r2, l3, r3, options, share, touchpad, ps").arg(b.toString());
			return false;
		}
		state->buttons |= it.value();
	}
	if(step.contains("l2"))
		state->l2_state = static_cast<uint8_t>(qRound(qBound(0.0, step.value("l2").toDouble(), 1.0) * 255.0));
	if(step.contains("r2"))
		state->r2_state = static_cast<uint8_t>(qRound(qBound(0.0, step.value("r2").toDouble(), 1.0) * 255.0));
	const QJsonObject left = step.value("left_stick").toObject();
	state->left_x = StickAxis(left.value("x").toDouble());
	state->left_y = StickAxis(left.value("y").toDouble());
	const QJsonObject right = step.value("right_stick").toObject();
	state->right_x = StickAxis(right.value("x").toDouble());
	state->right_y = StickAxis(right.value("y").toDouble());
	if(step.value("touch").isObject())
	{
		StreamSession *session = window && window->getBackend() ? window->getBackend()->qmlSession() : nullptr;
		const QJsonObject touch = step.value("touch").toObject();
		const float max_x = session ? session->GetTouchpadMaxX() : 1920.0f;
		const float max_y = session ? session->GetTouchpadMaxY() : 942.0f;
		const uint16_t tx = static_cast<uint16_t>(qBound(0.0, touch.value("x").toDouble(0.5), 1.0) * (max_x - 1));
		const uint16_t ty = static_cast<uint16_t>(qBound(0.0, touch.value("y").toDouble(0.5), 1.0) * (max_y - 1));
		chiaki_controller_state_start_touch(state, tx, ty);
	}
	return true;
}

void McpChiakiTools::RunControllerSteps(const QJsonArray &steps, int index, const McpServer::Responder &respond)
{
	StreamSession *session = window && window->getBackend() ? window->getBackend()->qmlSession() : nullptr;
	if(!session)
	{
		respond(McpServer::ErrorResult(QStringLiteral("The stream session ended")));
		return;
	}
	if(index >= steps.size())
	{
		ChiakiControllerState idle;
		chiaki_controller_state_set_idle(&idle);
		session->SetMcpControllerState(idle);
		respond(McpServer::TextResult(QStringLiteral("Sent %1 controller step(s)").arg(steps.size())));
		return;
	}

	const QJsonObject step = steps[index].toObject();
	ChiakiControllerState state;
	QString error;
	if(!StateFromStep(step, &state, &error))
	{
		ReleaseController();
		respond(McpServer::ErrorResult(QStringLiteral("Step %1: %2").arg(index).arg(error)));
		return;
	}
	session->SetMcpControllerState(state);

	const int hold_ms = qBound(16, step.value("hold_ms").toInt(100), kMaxControllerSequenceMs);
	const int wait_ms = qBound(0, step.value("wait_ms").toInt(120), kMaxControllerSequenceMs);
	const quint64 generation = controller_generation;
	QPointer<McpChiakiTools> self(this);
	QTimer::singleShot(hold_ms, this, [self, steps, index, wait_ms, generation, respond]() {
		if(!self)
			return;
		if(generation != self->controller_generation)
		{
			respond(McpServer::ErrorResult(QStringLiteral("Interrupted by another controller command or the session ending")));
			return;
		}
		StreamSession *session = self->window && self->window->getBackend() ? self->window->getBackend()->qmlSession() : nullptr;
		if(session)
		{
			ChiakiControllerState idle;
			chiaki_controller_state_set_idle(&idle);
			session->SetMcpControllerState(idle);
		}
		QTimer::singleShot(wait_ms, self.data(), [self, steps, index, generation, respond]() {
			if(!self)
				return;
			if(generation != self->controller_generation)
			{
				respond(McpServer::ErrorResult(QStringLiteral("Interrupted by another controller command or the session ending")));
				return;
			}
			self->RunControllerSteps(steps, index + 1, respond);
		});
	});
}

void McpChiakiTools::AddExamples()
{
	auto parse = [](const char *json) { return QJsonDocument::fromJson(QByteArray(json)).array(); };
	const QHash<QString, QJsonArray> examples = {
		{"take_screenshot", parse(R"([{"annotate": true}, {"max_width": 1280}, {"region": {"x": 0, "y": 600, "width": 640, "height": 120}}, {"save_path": "C:/temp/chiaki.png"}])")},
		{"find_ui_elements", parse(R"([{"text": "Settings"}, {"type": "Button"}, {"type": "CheckBox", "text": "HDR"}])")},
		{"click_element", parse(R"([{"id": "0/1.3.2"}, {"text": "OK", "type": "Button"},
			{"text": "Settings", "expect": {"until": "element", "text": "Video"}}])")},
		{"get_ui_tree", parse(R"([{"interactive_only": true}, {"root": "0/0.2.1"}, {"window": 1, "format": "json"}])")},
		{"focus_element", parse(R"([{"text": "Resolution"}])")},
		{"click", parse(R"([{"x": 640, "y": 360}, {"x": 0.5, "y": 0.9, "coordinate_space": "normalized"},
			{"x": 200, "y": 150, "clicks": 2}, {"x": 300, "y": 300, "button": "right"}])")},
		{"drag", parse(R"([{"from_x": 100, "from_y": 500, "to_x": 600, "to_y": 500}])")},
		{"scroll", parse(R"([{"x": 640, "y": 400, "delta_y": 3}])")},
		{"press_key", parse(R"([{"key": "Return"}, {"key": "Down", "repeat": 3}, {"key": "ctrl+o"},
			{"key": "Escape", "expect": {"until": "screen_changed"}}])")},
		{"type_text", parse(R"([{"text": "192.168.1.20"}, {"text": "hello\n"}])")},
		{"wait_for", parse(R"([{"until": "element", "text": "Connected"}, {"until": "element_gone", "text": "Loading"},
			{"until": "screen_stable", "timeout_ms": 15000}, {"until": "screen_changed", "timeout_ms": 3000}])")},
		{"connect_host", parse(R"([{"index": 0}, {"name": "PS5-123"}])")},
		{"wait_for_stream", parse(R"([{"state": "video"}, {"state": "ended", "timeout_ms": 10000}])")},
		{"controller_input", parse(R"([{"buttons": ["cross"]},
			{"sequence": ["dpad_down", "dpad_down", "cross"], "expect": {"until": "screen_changed"}},
			{"buttons": ["ps"], "hold_ms": 1000},
			{"steps": [{"left_stick": {"x": 0, "y": -1}, "hold_ms": 800}, {"buttons": ["r2"], "hold_ms": 300}]},
			{"touch": {"x": 0.5, "y": 0.5}, "buttons": ["touchpad"]}])")},
		{"window_control", parse(R"([{"action": "activate"}, {"action": "resize", "width": 1280, "height": 720}])")},
		{"disconnect_stream", parse(R"([{}, {"sleep_console": true}])")},
	};
	for(auto it = examples.constBegin(); it != examples.constEnd(); ++it)
		server->AddToolExamples(it.key(), it.value());
}

void McpChiakiTools::RegisterPrompts()
{
	const QString guide = QString::fromUtf8(kUsageGuide);

	server->AddPrompt({
		"play_on_console",
		"Do something on the PlayStation",
		"Connect to a console if needed and accomplish a goal on it (launch a game, change a console setting, ...).",
		{
			{"goal", "What to do on the console, e.g. \"open Gran Turismo 7\"", true},
			{"console", "Console name or index (default: the only/first ready one)", false},
		},
		[guide](const QJsonObject &args) {
			const QString console = args.value("console").toString();
			return QStringLiteral(
				"Use the chiaki-ng MCP tools to do this on my PlayStation: %1\n\n"
				"Plan:\n"
				"1. get_app_state. If no stream is running: list_hosts, pick %2, connect_host, then wait_for_stream "
				"(state video) until the console picture appears.\n"
				"2. Work in a look-act-verify loop: read the screenshot, send controller_input with screenshot_after=true, "
				"check the result. Navigate with dpad, confirm with cross, back with circle, ps for home.\n"
				"3. Stop and ask me if a PIN, password, purchase or anything irreversible comes up.\n"
				"4. Tell me what you did and show the final screen.\n\n%3")
				.arg(args.value("goal").toString(),
					console.isEmpty() ? QStringLiteral("the console that is ready (or the first one)") : QStringLiteral("\"%1\"").arg(console),
					guide);
		},
	});

	server->AddPrompt({
		"operate_chiaki",
		"Use the chiaki-ng app",
		"Accomplish a task in the chiaki-ng interface itself (settings, profiles, consoles, registration, stream menu).",
		{{"goal", "What to do, e.g. \"set the stream resolution to 1080p\"", true}},
		[guide](const QJsonObject &args) {
			return QStringLiteral(
				"Use the chiaki-ng MCP tools to do this in the chiaki-ng app: %1\n\n"
				"Start with take_screenshot annotate=true and get_ui_tree to understand the current view. Prefer "
				"click_element by id, verify every step with screenshot_after=true, and use Escape to back out of "
				"wrong dialogs. Do not change unrelated settings. Report what changed.\n\n%2")
				.arg(args.value("goal").toString(), guide);
		},
	});

	server->AddPrompt({
		"diagnose_stream",
		"Diagnose stream quality",
		"Inspect the running stream (bitrate, packet loss, dropped frames, picture) and suggest fixes.",
		{},
		[](const QJsonObject &) {
			return QStringLiteral(
				"Diagnose my chiaki-ng Remote Play stream. Call get_app_state twice about 5 s apart (wait tool) to see "
				"bitrate, packet loss, frames lost and dropped frames trends, take a screenshot to judge picture quality "
				"(blocking, blur, black screen), then explain the likely cause (Wi-Fi, bitrate/resolution too high, decoder, "
				"console) and concrete chiaki-ng settings to change. Do not change settings without asking me.");
		},
	});
}

void McpChiakiTools::RegisterTools()
{
	ui_tools->RegisterTools(server);
	QPointer<McpChiakiTools> self(this);

	auto backend_or_error = [self](const McpServer::Responder &respond) -> QmlBackend * {
		QmlBackend *backend = self && self->window ? self->window->getBackend() : nullptr;
		if(!backend)
			respond(McpServer::ErrorResult(QStringLiteral("The application is shutting down")));
		return backend;
	};

	server->AddTool({
		"get_usage_guide",
		"Get usage guide",
		"Read this first: how to drive chiaki-ng and the PlayStation effectively (look-act-verify loop, controller cheat "
		"sheet, timings, shortcuts, troubleshooting).",
		Schema({}),
		true,
		[](const QJsonObject &, const McpServer::Responder &respond) {
			respond(McpServer::TextResult(QString::fromUtf8(kUsageGuide)));
		},
	});

	server->AddTool({
		"mcp_self_check",
		"Self check",
		"Verify that controlling chiaki-ng works right now: window visibility, frame capture, UI access, keyboard "
		"focus, consoles and the stream. Each check reports pass/warn/fail with a fix. Run it first if something "
		"behaves unexpectedly.",
		Schema({}),
		true,
		[self](const QJsonObject &, const McpServer::Responder &respond) {
			if(!self)
				return;
			self->ui_tools->SelfCheck([self, respond](const QJsonArray &generic) {
				if(!self)
					return;
				QJsonArray checks = generic;
				QmlBackend *backend = self->window ? self->window->getBackend() : nullptr;
				if(backend)
				{
					const QVariantList hosts = backend->hosts();
					int registered = 0, ready = 0;
					for(const QVariant &h : hosts)
					{
						const QVariantMap m = h.toMap();
						if(m.value("registered").toBool())
							registered++;
						if(m.value("state").toString() == QLatin1String("ready"))
							ready++;
					}
					QJsonObject consoles{{"check", "consoles"}};
					if(hosts.isEmpty())
					{
						consoles["status"] = "warn";
						consoles["detail"] = "No consoles found";
						consoles["fix"] = "Make sure the PlayStation is on the same network with Remote Play enabled, or add it manually in chiaki-ng";
					}
					else
					{
						consoles["status"] = registered ? "pass" : "warn";
						consoles["detail"] = QStringLiteral("%1 console(s), %2 registered, %3 ready (others may be in rest mode)")
							.arg(hosts.size()).arg(registered).arg(ready);
						if(!registered)
							consoles["fix"] = "Register a console first (connect_host opens the registration dialog; the user must supply the PIN)";
					}
					checks.append(consoles);

					StreamSession *session = backend->qmlSession();
					QJsonObject stream{{"check", "stream"}};
					if(!session)
					{
						stream["status"] = "pass";
						stream["detail"] = "No stream running (controller_input needs one)";
					}
					else if(!session->IsConnected())
					{
						stream["status"] = "warn";
						stream["detail"] = "Stream is connecting";
						stream["fix"] = "wait_for_stream {state: video}";
					}
					else
					{
						const double loss = session->GetAveragePacketLoss();
						stream["status"] = loss > 0.05 || !self->window->hasVideo() ? "warn" : "pass";
						stream["detail"] = QStringLiteral("Connected to %1, video %2, %3 Mbit/s, packet loss %4%")
							.arg(session->GetHost(), self->window->hasVideo() ? QStringLiteral("visible") : QStringLiteral("not yet visible"))
							.arg(session->GetMeasuredBitrate(), 0, 'f', 1).arg(loss * 100.0, 0, 'f', 1);
						if(loss > 0.05)
							stream["fix"] = "High packet loss: lower resolution/bitrate in chiaki-ng settings or use a wired connection";
					}
					checks.append(stream);
				}
				int failures = 0, warnings = 0;
				for(const QJsonValue &c : checks)
				{
					const QString status = c.toObject().value("status").toString();
					failures += status == QLatin1String("fail");
					warnings += status == QLatin1String("warn");
				}
				const QJsonObject report{
					{"overall", failures ? "fail" : (warnings ? "warn" : "pass")},
					{"checks", checks},
					{"endpoint", self->server->Endpoint()},
				};
				respond(McpServer::TextResult(ToJson(report)));
			});
		},
	});

	server->AddTool({
		"get_action_history",
		"Get action history",
		"The tool calls made in this session (newest last) with arguments, outcome, errors and duration. Use it to "
		"recall what was already tried after a long task, or to avoid repeating a failed approach.",
		Schema({
			{"limit", Prop("integer", "How many recent calls (default 20, max 100)")},
			{"errors_only", Prop("boolean", "Only calls that failed")},
		}),
		true,
		[self](const QJsonObject &args, const McpServer::Responder &respond) {
			if(!self)
				return;
			const int limit = qBound(1, args.value("limit").toInt(20), 100);
			const bool errors_only = args.value("errors_only").toBool(false);
			const QList<McpServer::HistoryEntry> history = self->server->History();
			QStringList lines;
			for(int i = history.size() - 1; i >= 0 && lines.size() < limit; i--)
			{
				const McpServer::HistoryEntry &e = history[i];
				if(errors_only && !e.is_error)
					continue;
				QJsonObject shown_args = e.arguments;
				for(const QString &key : shown_args.keys())
					if(shown_args.value(key).toString().size() > 80)
						shown_args[key] = shown_args.value(key).toString().left(77) + QStringLiteral("...");
				lines.prepend(QStringLiteral("%1 %2%3 %4 (%5 ms) -> %6")
						.arg(QDateTime::fromMSecsSinceEpoch(e.timestamp_ms).toString(QStringLiteral("HH:mm:ss")))
						.arg(e.is_error ? QStringLiteral("FAILED ") : QString())
						.arg(e.tool)
						.arg(QString::fromUtf8(QJsonDocument(shown_args).toJson(QJsonDocument::Compact)))
						.arg(e.duration_ms)
						.arg(e.outcome));
			}
			respond(McpServer::TextResult(lines.isEmpty() ? QStringLiteral("No tool calls recorded yet") : lines.join(QLatin1Char('\n'))));
		},
	});

	server->AddTool({
		"get_app_state",
		"Get app state",
		"Summarize chiaki-ng: whether a stream is running (host, connection, bitrate, packet loss, video), "
		"console count, PSN state, controllers and the window state.",
		Schema({}),
		true,
		[self](const QJsonObject &, const McpServer::Responder &respond) {
			if(!self)
				return;
			respond(McpServer::TextResult(ToJson(self->AppState())));
		},
	});

	server->AddTool({
		"list_hosts",
		"List consoles",
		"List the PlayStation consoles chiaki-ng knows (discovered on the LAN, manual and PSN remote hosts) with "
		"their index, name, address, state (ready/standby), running app and whether they are registered.",
		Schema({}),
		true,
		[backend_or_error](const QJsonObject &, const McpServer::Responder &respond) {
			QmlBackend *backend = backend_or_error(respond);
			if(!backend)
				return;
			QJsonArray hosts;
			const QVariantList list = backend->hosts();
			for(int i = 0; i < list.size(); i++)
			{
				QJsonObject h = QJsonObject::fromVariantMap(list[i].toMap());
				h["index"] = i;
				hosts.append(h);
			}
			respond(McpServer::TextResult(ToJson(hosts)));
		},
	});

	auto find_host = [](QmlBackend *backend, const QJsonObject &args, int *index, QVariantMap *host, QString *error) -> bool {
		const QVariantList list = backend->hosts();
		if(args.value("index").isDouble())
		{
			*index = args.value("index").toInt();
			if(*index < 0 || *index >= list.size())
			{
				*error = QStringLiteral("No console at index %1 (%2 known)").arg(*index).arg(list.size());
				return false;
			}
			*host = list[*index].toMap();
			return true;
		}
		const QString name = args.value("name").toString();
		if(name.isEmpty())
		{
			*error = QStringLiteral("Pass index or name (see list_hosts)");
			return false;
		}
		for(int i = 0; i < list.size(); i++)
		{
			const QVariantMap m = list[i].toMap();
			if(m.value("name").toString().compare(name, Qt::CaseInsensitive) == 0 || m.value("address").toString() == name)
			{
				*index = i;
				*host = m;
				return true;
			}
		}
		*error = QStringLiteral("No console named '%1'").arg(name);
		return false;
	};

	const QJsonObject host_props{
		{"index", Prop("integer", "Console index from list_hosts")},
		{"name", Prop("string", "Console name or address (alternative to index)")},
	};

	server->AddTool({
		"connect_host",
		"Connect to console",
		"Start a Remote Play stream with a console (wakes it from rest mode first when needed). Unregistered "
		"consoles open the registration dialog instead. Follow up with wait + take_screenshot.",
		Schema(host_props),
		false,
		[backend_or_error, find_host](const QJsonObject &args, const McpServer::Responder &respond) {
			QmlBackend *backend = backend_or_error(respond);
			if(!backend)
				return;
			int index = -1;
			QVariantMap host;
			QString error;
			if(!find_host(backend, args, &index, &host, &error))
			{
				respond(McpServer::ErrorResult(error));
				return;
			}
			if(backend->qmlSession())
			{
				respond(McpServer::ErrorResult(QStringLiteral("A stream is already running; call disconnect_stream first")));
				return;
			}
			// Same as clicking the console in the UI.
			if(host.value("discovered").toBool())
				backend->connectToHost(index, host.value("name").toString());
			else
				backend->connectToHost(index);
			respond(McpServer::TextResult(host.value("registered").toBool()
					? QStringLiteral("Connecting to %1 ...").arg(host.value("name").toString())
					: QStringLiteral("%1 is not registered; the registration dialog was opened").arg(host.value("name").toString())));
		},
	});

	server->AddTool({
		"wake_host",
		"Wake console",
		"Wake a registered manual console from rest mode (discovered consoles are woken automatically by connect_host).",
		Schema(host_props),
		false,
		[backend_or_error, find_host](const QJsonObject &args, const McpServer::Responder &respond) {
			QmlBackend *backend = backend_or_error(respond);
			if(!backend)
				return;
			int index = -1;
			QVariantMap host;
			QString error;
			if(!find_host(backend, args, &index, &host, &error))
			{
				respond(McpServer::ErrorResult(error));
				return;
			}
			if(host.value("discovered").toBool() || !host.value("duid").toString().isEmpty())
			{
				respond(McpServer::ErrorResult(QStringLiteral("%1 is woken by connect_host").arg(host.value("name").toString())));
				return;
			}
			backend->wakeUpHost(index);
			respond(McpServer::TextResult(QStringLiteral("Wake-up sent to %1").arg(host.value("name").toString())));
		},
	});

	server->AddTool({
		"disconnect_stream",
		"Disconnect stream",
		"End the current Remote Play stream, optionally putting the console into rest mode.",
		Schema({{"sleep_console", Prop("boolean", "Put the console into rest mode (default false)")}}),
		false,
		[self, backend_or_error](const QJsonObject &args, const McpServer::Responder &respond) {
			QmlBackend *backend = backend_or_error(respond);
			if(!backend)
				return;
			if(!backend->qmlSession())
			{
				respond(McpServer::ErrorResult(QStringLiteral("No stream is running")));
				return;
			}
			if(self)
				self->ReleaseController();
			backend->stopSession(args.value("sleep_console").toBool(false));
			respond(McpServer::TextResult(QStringLiteral("Disconnecting")));
		},
	});

	const QJsonObject stick{
		{"type", "object"},
		{"properties", QJsonObject{{"x", Prop("number", "-1 (left) .. 1 (right)")}, {"y", Prop("number", "-1 (up) .. 1 (down)")}}},
		{"description", "Analog stick position"},
	};
	const QJsonObject step_props{
		{"buttons", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}},
			{"description", "Buttons held together: cross, circle, square, triangle, dpad_up, dpad_down, dpad_left, dpad_right, "
				"l1, r1, l2, r2, l3, r3, options, share, touchpad, ps"}}},
		{"left_stick", stick},
		{"right_stick", stick},
		{"l2", Prop("number", "L2 trigger pressure 0..1")},
		{"r2", Prop("number", "R2 trigger pressure 0..1")},
		{"touch", QJsonObject{{"type", "object"}, {"description", "Touch the touchpad at x/y in 0..1 (combine with buttons:[touchpad] to click)"},
			{"properties", QJsonObject{{"x", Prop("number", "0..1")}, {"y", Prop("number", "0..1")}}}}},
		{"hold_ms", Prop("integer", "How long the input is held (default 100)")},
		{"wait_ms", Prop("integer", "Pause after releasing (default 120)")},
	};
	QJsonObject controller_props = step_props;
	controller_props["sequence"] = QJsonObject{
		{"type", "array"}, {"items", QJsonObject{{"type", "string"}}},
		{"description", "Shorthand: buttons pressed one after another, e.g. [\"dpad_down\",\"dpad_down\",\"cross\"]"},
	};
	controller_props["steps"] = QJsonObject{
		{"type", "array"},
		{"items", QJsonObject{{"type", "object"}, {"properties", step_props}}},
		{"description", "Full control: each step sets buttons/sticks/triggers/touch, holds, releases and waits"},
	};

	server->AddTool({
		"controller_input",
		"Controller input",
		"Press DualSense/DualShock buttons, move sticks and triggers or touch the touchpad on the streamed console "
		"(merged with any physical controller). Use buttons for a single press, sequence for several presses, or "
		"steps for combos and held stick movement. Returns after the inputs were released.",
		Schema(controller_props),
		false,
		[self](const QJsonObject &args, const McpServer::Responder &respond) {
			if(!self || !self->window || !self->window->getBackend() || !self->window->getBackend()->qmlSession())
			{
				respond(McpServer::ErrorResult(QStringLiteral("No stream is running, so there is no console to send controller input to. "
						"Use list_hosts, connect_host, then wait_for_stream {state: video}. To drive chiaki's own menus use "
						"click_element / press_key instead.")));
				return;
			}
			QJsonArray steps;
			if(args.value("steps").isArray())
				steps = args.value("steps").toArray();
			else if(args.value("sequence").isArray())
			{
				for(const QJsonValue &b : args.value("sequence").toArray())
				{
					QJsonObject step{{"buttons", QJsonArray{b}}};
					if(args.contains("hold_ms"))
						step["hold_ms"] = args.value("hold_ms");
					if(args.contains("wait_ms"))
						step["wait_ms"] = args.value("wait_ms");
					steps.append(step);
				}
			}
			else
				steps.append(args);
			if(steps.isEmpty())
			{
				respond(McpServer::ErrorResult(QStringLiteral("Nothing to send")));
				return;
			}
			qint64 total_ms = 0;
			for(const QJsonValue &s : steps)
				// Same clamping as RunControllerSteps, so negative values cannot hide long holds.
				total_ms += qBound(16, s.toObject().value("hold_ms").toInt(100), kMaxControllerSequenceMs) +
					qBound(0, s.toObject().value("wait_ms").toInt(120), kMaxControllerSequenceMs);
			if(total_ms > kMaxControllerSequenceMs)
			{
				respond(McpServer::ErrorResult(QStringLiteral("Sequence too long (%1 ms, max %2 ms); split it up").arg(total_ms).arg(kMaxControllerSequenceMs)));
				return;
			}
			// A new command replaces whatever is still running.
			self->controller_generation++;
			self->RunControllerSteps(steps, 0, respond);
		},
	});

	server->AddTool({
		"wait_for_stream",
		"Wait for stream",
		"After connect_host or disconnect_stream: wait until the stream is connected, the console picture is visible "
		"(video), or the session ended. Returns immediately once true, with a screenshot.",
		Schema({
			{"state", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"connected", "video", "ended"}}, {"description", "State to wait for (default video)"}}},
			{"timeout_ms", Prop("integer", "Give up after this long (default 30000, max 120000)")},
			{"screenshot", Prop("boolean", "Take a screenshot when done (default true)")},
		}),
		true,
		[self](const QJsonObject &args, const McpServer::Responder &respond) {
			if(!self)
				return;
			const QString wanted = args.value("state").toString(QStringLiteral("video"));
			const int timeout_ms = qBound(0, args.value("timeout_ms").toInt(30000), 120000);
			auto elapsed = QSharedPointer<QElapsedTimer>::create();
			elapsed->start();
			auto poll = QSharedPointer<std::function<void()>>::create();
			*poll = [self, wanted, timeout_ms, elapsed, poll, args, respond]() {
				if(!self || !self->window || !self->window->getBackend())
					return;
				StreamSession *session = self->window->getBackend()->qmlSession();
				bool met = false;
				if(wanted == QLatin1String("ended"))
					met = !session;
				else if(wanted == QLatin1String("connected"))
					met = session && session->IsConnected();
				else
					met = session && session->IsConnected() && self->window->hasVideo();
				const bool timed_out = !met && elapsed->elapsed() >= timeout_ms;
				if(!met && !timed_out)
				{
					QTimer::singleShot(250, self.data(), [poll]() { (*poll)(); });
					return;
				}
				// The closure references itself; release it after this event.
				QTimer::singleShot(0, self.data(), [poll]() { *poll = nullptr; });
				QJsonObject summary = self->AppState();
				summary["condition_met"] = met;
				summary["timed_out"] = timed_out;
				summary["elapsed_ms"] = qint64(elapsed->elapsed());
				const QJsonObject text_result = McpServer::TextResult(ToJson(summary));
				if(!args.value("screenshot").toBool(true))
				{
					respond(text_result);
					return;
				}
				self->ui_tools->TakeScreenshot(QJsonObject(), [text_result, respond](const QJsonObject &shot) {
					QJsonArray content = text_result.value("content").toArray();
					if(!shot.value("isError").toBool(false))
						for(const QJsonValue &c : shot.value("content").toArray())
							content.append(c);
					respond(QJsonObject{{"content", content}});
				});
			};
			(*poll)();
		},
	});

	server->AddTool({
		"release_controller",
		"Release controller",
		"Immediately release every virtual button, stick and trigger held by controller_input.",
		Schema({}),
		false,
		[self](const QJsonObject &, const McpServer::Responder &respond) {
			if(self)
				self->ReleaseController();
			respond(McpServer::TextResult(QStringLiteral("Released")));
		},
	});

	AddExamples();

	ui_tools->EnableScreenshotAfter(server, {
		"click", "click_element", "focus_element", "mouse_move", "drag", "scroll", "press_key", "type_text",
		"window_control", "controller_input", "connect_host", "wake_host", "disconnect_stream",
	});
}
