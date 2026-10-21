// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <mcpuitools.h>

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImageWriter>
#include <QJsonDocument>
#include <QKeySequence>
#include <QPainter>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScreen>
#include <QSharedPointer>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <QtMath>
#include <qpa/qwindowsysteminterface.h>

#include <algorithm>
#include <cmath>

namespace
{
using Delivery = QWindowSystemInterface::SynchronousDelivery;

constexpr qint64 kDefaultMaxImageBytes = 3500 * 1024; // stays below the ~5 MB base64 limit common in AI clients
constexpr int kMaxWaitMs = 30000;

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

QJsonObject EnumProp(const QStringList &values, const QString &description)
{
	return QJsonObject{{"type", "string"}, {"enum", QJsonArray::fromStringList(values)}, {"description", description}};
}

QJsonObject CoordinateSpaceProp()
{
	return EnumProp({"screenshot", "window_pixels", "logical", "normalized"},
			"How x/y are measured. screenshot (default): pixels of the most recent take_screenshot image. "
			"window_pixels: physical pixels of the main window. logical: Qt logical pixels of the main window. "
			"normalized: 0..1 of the main window.");
}

QJsonObject ModifiersProp()
{
	return QJsonObject{
		{"type", "array"},
		{"items", EnumProp({"shift", "ctrl", "alt", "meta"}, "Modifier key")},
		{"description", "Modifier keys held during the action"},
	};
}

QString ToJson(const QJsonObject &object)
{
	return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

QString CleanTypeName(const QObject *object)
{
	QString name = QString::fromLatin1(object->metaObject()->className());
	// Types derived in QML can carry several generated suffixes, e.g. Button_QMLTYPE_13_QML_15.
	static const QRegularExpression qml_suffix(QStringLiteral("(_QML(TYPE)?_\\d+)+$"));
	name.remove(qml_suffix);
	if(name.startsWith(QLatin1String("QQuick")) && name.size() > 6)
		name = name.mid(6);
	if(name.endsWith(QLatin1String("Impl")) && name.size() > 4)
		name.chop(4);
	return name;
}

bool IsInteractiveType(const QString &type)
{
	// Style implementation details (e.g. the native look of a CheckBox) are not targets.
	if(type.startsWith(QLatin1String("StyleItem")) || type.startsWith(QLatin1String("NativeStyle")))
		return false;
	static const QStringList kinds = {
		"Button", "CheckBox", "Switch", "Slider", "ComboBox", "TextField", "TextInput", "TextEdit", "TextArea",
		"SpinBox", "RadioButton", "Delegate", "MenuItem", "TabButton", "MouseArea", "ListView", "GridView",
		"Popup", "Dialog", "Drawer", "Menu", "ToolTip", "Flickable", "ScrollView", "StackView", "SwipeView",
		"RangeSlider", "Dial", "Tumbler", "WebEngineView", "Control",
	};
	for(const QString &kind : kinds)
		if(type.contains(kind))
			return true;
	return false;
}

QString ItemText(const QQuickItem *item)
{
	static const char *const props[] = {"text", "title", "currentText", "displayText", "placeholderText", "label"};
	for(const char *prop : props)
	{
		const QVariant value = item->property(prop);
		if(value.metaType().id() != QMetaType::QString)
			continue;
		QString text = value.toString().simplified();
		if(text.isEmpty())
			continue;
		// Never leak passwords or PINs typed into masked fields.
		const QVariant echo_mode = item->property("echoMode");
		if(qstrcmp(prop, "placeholderText") != 0 && echo_mode.isValid() && echo_mode.toInt() != 0)
			return QStringLiteral("<masked>");
		if(text.size() > 160)
			text = text.left(157) + QStringLiteral("...");
		return text;
	}
	return QString();
}

Qt::KeyboardModifiers ParseModifiers(const QJsonValue &value)
{
	Qt::KeyboardModifiers mods = Qt::NoModifier;
	for(const QJsonValue &v : value.toArray())
	{
		const QString m = v.toString().toLower();
		if(m == QLatin1String("shift"))
			mods |= Qt::ShiftModifier;
		else if(m == QLatin1String("ctrl") || m == QLatin1String("control"))
			mods |= Qt::ControlModifier;
		else if(m == QLatin1String("alt") || m == QLatin1String("option"))
			mods |= Qt::AltModifier;
		else if(m == QLatin1String("meta") || m == QLatin1String("cmd") || m == QLatin1String("win") || m == QLatin1String("super"))
			mods |= Qt::MetaModifier;
	}
	return mods;
}

Qt::MouseButton ParseButton(const QString &name)
{
	const QString b = name.toLower();
	if(b == QLatin1String("right"))
		return Qt::RightButton;
	if(b == QLatin1String("middle"))
		return Qt::MiddleButton;
	if(b == QLatin1String("back"))
		return Qt::BackButton;
	if(b == QLatin1String("forward"))
		return Qt::ForwardButton;
	return Qt::LeftButton;
}

QString WindowDescription(const QWindow *window)
{
	if(!window)
		return QStringLiteral("<none>");
	QString name = window->objectName();
	if(name.isEmpty())
		name = window->title();
	if(name.isEmpty())
		name = CleanTypeName(window);
	return name;
}

QString WindowKind(const QWindow *window)
{
	switch(window->type())
	{
		case Qt::Dialog: return QStringLiteral("dialog");
		case Qt::Tool: return QStringLiteral("tool");
		case Qt::Popup: return QStringLiteral("popup");
		case Qt::ToolTip: return QStringLiteral("tooltip");
		case Qt::SplashScreen: return QStringLiteral("splash");
		default: return QStringLiteral("window");
	}
}

int PaintRank(const QWindow *window)
{
	// Qt does not expose the native stacking order; approximate it by window type.
	if(window->flags().testFlag(Qt::WindowStaysOnTopHint))
		return 5;
	switch(window->type())
	{
		case Qt::ToolTip: return 6;
		case Qt::Popup: return 4;
		case Qt::Tool: return 3;
		case Qt::Dialog: return 2;
		default: return 1;
	}
}

QString CanonicalKeyToken(const QString &token)
{
	const QString t = token.trimmed().toLower();
	static const QHash<QString, QString> aliases = {
		{"enter", "Return"}, {"return", "Return"}, {"esc", "Escape"}, {"escape", "Escape"},
		{"up", "Up"}, {"down", "Down"}, {"left", "Left"}, {"right", "Right"},
		{"arrowup", "Up"}, {"arrowdown", "Down"}, {"arrowleft", "Left"}, {"arrowright", "Right"},
		{"space", "Space"}, {"spacebar", "Space"}, {"tab", "Tab"}, {"backspace", "Backspace"},
		{"del", "Delete"}, {"delete", "Delete"}, {"ins", "Insert"}, {"insert", "Insert"},
		{"home", "Home"}, {"end", "End"}, {"pageup", "PgUp"}, {"pgup", "PgUp"},
		{"pagedown", "PgDown"}, {"pgdown", "PgDown"}, {"pgdn", "PgDown"}, {"menu", "Menu"},
		{"back", "Back"}, {"plus", "+"}, {"minus", "-"},
	};
	auto it = aliases.constFind(t);
	if(it != aliases.constEnd())
		return it.value();
	return token.trimmed();
}
}

McpUiTools::McpUiTools(QWindow *main_window, QQuickWindow *main_scene, QObject *parent)
	: QObject(parent)
	, main_window(main_window)
	, main_scene(main_scene)
{
	main_grabber = [this](const FrameCallback &callback) {
		QQuickWindow *quick = qobject_cast<QQuickWindow *>(this->main_window.data());
		if(!quick)
		{
			callback(QImage(), QStringLiteral("No frame grabber configured for the main window"));
			return;
		}
		callback(quick->grabWindow(), QString());
	};
}

double McpUiTools::MainDpr() const
{
	return main_window ? main_window->devicePixelRatio() : 1.0;
}

QList<McpUiTools::Surface> McpUiTools::Surfaces() const
{
	QList<Surface> surfaces;
	if(!main_window)
		return surfaces;

	Surface main;
	main.window = main_window;
	main.scene = main_scene;
	main.global_rect = main_window->geometry();
	main.kind = QStringLiteral("main");
	main.is_main = true;
	surfaces.append(main);

	QHash<QWindow *, QWidget *> widget_windows;
	if(qobject_cast<QApplication *>(QCoreApplication::instance()))
	{
		for(QWidget *widget : QApplication::topLevelWidgets())
			if(widget->windowHandle())
				widget_windows.insert(widget->windowHandle(), widget);
	}

	QList<Surface> others;
	for(QWindow *window : QGuiApplication::topLevelWindows())
	{
		if(window == main_window || excluded_windows.contains(window) || window == main_scene)
			continue;
		if(!window->isVisible() || window->visibility() == QWindow::Minimized || window->opacity() <= 0.0)
			continue;
		if(window->width() <= 0 || window->height() <= 0)
			continue;
		if(window->type() == Qt::Desktop)
			continue;
		Surface s;
		s.window = window;
		s.scene = qobject_cast<QQuickWindow *>(window);
		s.widget = widget_windows.value(window);
		if(!s.scene && !s.widget)
			continue; // nothing we know how to render
		s.global_rect = window->geometry();
		s.kind = WindowKind(window);
		s.accepts_input = !window->flags().testFlag(Qt::WindowTransparentForInput);
		others.append(s);
	}
	// Element ids embed the surface index, so this order must not depend on focus.
	std::stable_sort(others.begin(), others.end(), [](const Surface &a, const Surface &b) {
		return PaintRank(a.window) < PaintRank(b.window);
	});
	surfaces.append(others);
	return surfaces;
}

McpUiTools::Mapping McpUiTools::CurrentMapping(const QList<Surface> &surfaces) const
{
	Mapping mapping;
	if(surfaces.isEmpty())
		return mapping;
	QRect canvas = surfaces.first().global_rect;
	for(const Surface &s : surfaces)
		canvas = canvas.united(s.global_rect);
	mapping.valid = true;
	mapping.scale = MainDpr();
	mapping.origin_offset = QPointF(canvas.topLeft() - surfaces.first().global_rect.topLeft());
	mapping.image_size = QSize(qCeil(canvas.width() * mapping.scale), qCeil(canvas.height() * mapping.scale));
	return mapping;
}

McpUiTools::Mapping McpUiTools::ActiveMapping() const
{
	if(last_mapping.valid)
		return last_mapping;
	return CurrentMapping(Surfaces());
}

QPointF McpUiTools::ImageToGlobal(const QPointF &image_point, const Mapping &mapping) const
{
	const QPointF main_origin = main_window ? QPointF(main_window->geometry().topLeft()) : QPointF();
	return main_origin + mapping.origin_offset + image_point / mapping.scale;
}

QRectF McpUiTools::GlobalToImage(const QRectF &global_rect, const Mapping &mapping) const
{
	const QPointF main_origin = main_window ? QPointF(main_window->geometry().topLeft()) : QPointF();
	const QPointF top_left = (global_rect.topLeft() - main_origin - mapping.origin_offset) * mapping.scale;
	return QRectF(top_left, global_rect.size() * mapping.scale);
}

bool McpUiTools::PointFromArgs(const QJsonObject &args, const QString &x_key, const QString &y_key, QPointF *global, QString *error) const
{
	if(!main_window)
	{
		*error = QStringLiteral("The main window is gone");
		return false;
	}
	if(!args.value(x_key).isDouble() || !args.value(y_key).isDouble())
	{
		*error = QStringLiteral("'%1' and '%2' must be numbers").arg(x_key, y_key);
		return false;
	}
	const QPointF p(args.value(x_key).toDouble(), args.value(y_key).toDouble());
	const QString space = args.value("coordinate_space").toString(QStringLiteral("screenshot"));
	const QPointF main_origin(main_window->geometry().topLeft());
	if(space == QLatin1String("screenshot"))
		*global = ImageToGlobal(p, ActiveMapping());
	else if(space == QLatin1String("window_pixels"))
		*global = main_origin + p / MainDpr();
	else if(space == QLatin1String("logical"))
		*global = main_origin + p;
	else if(space == QLatin1String("normalized"))
		*global = main_origin + QPointF(p.x() * main_window->width(), p.y() * main_window->height());
	else
	{
		*error = QStringLiteral("Unknown coordinate_space '%1'").arg(space);
		return false;
	}
	return true;
}

QList<int> McpUiTools::PaintOrder(const QList<Surface> &surfaces) const
{
	QList<int> order;
	for(int i = 0; i < surfaces.size(); i++)
		order.append(i);
	// Same rank as Surfaces(), but the focused window is most likely on top of its peers.
	std::stable_sort(order.begin(), order.end(), [&surfaces](int a, int b) {
		if(surfaces[a].is_main != surfaces[b].is_main)
			return surfaces[a].is_main;
		if(!surfaces[a].window || !surfaces[b].window)
			return false;
		const int ra = PaintRank(surfaces[a].window), rb = PaintRank(surfaces[b].window);
		if(ra != rb)
			return ra < rb;
		QWindow *focus = QGuiApplication::focusWindow();
		return surfaces[b].window == focus && surfaces[a].window != focus;
	});
	return order;
}

int McpUiTools::SurfaceAt(const QList<Surface> &surfaces, const QPointF &global) const
{
	const QList<int> order = PaintOrder(surfaces);
	for(int k = order.size() - 1; k >= 0; k--)
	{
		const int i = order[k];
		const Surface &s = surfaces[i];
		if(!s.accepts_input || !s.window)
			continue;
		if(QRectF(s.global_rect).contains(global))
			return i;
	}
	// Outside every window: deliver to the main window anyway (Qt clips as needed).
	return surfaces.isEmpty() ? -1 : 0;
}

QWindow *McpUiTools::KeyboardTarget(const QList<Surface> &surfaces) const
{
	QWindow *focus = QGuiApplication::focusWindow();
	for(const Surface &s : surfaces)
		if(s.window == focus && s.accepts_input)
			return focus;
	return main_window;
}

void McpUiTools::EnsureKeyboardTarget(bool activate)
{
	if(!activate)
		return;
	QWindow *target = KeyboardTarget(Surfaces());
	if(target && !target->isActive())
	{
		target->requestActivate();
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
	}
}

void McpUiTools::SendMouseMove(const QPointF &global, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers)
{
	const QList<Surface> surfaces = Surfaces();
	const int index = SurfaceAt(surfaces, global);
	if(index < 0)
		return;
	QWindow *window = surfaces[index].window;
	QWindowSystemInterface::handleMouseEvent<Delivery>(window, window->mapFromGlobal(global), global,
			buttons, Qt::NoButton, QEvent::MouseMove, modifiers);
	last_mouse_global = global;
	has_last_mouse = true;
}

void McpUiTools::SendMouseButton(const QPointF &global, Qt::MouseButton button, bool press, Qt::MouseButtons buttons_after, Qt::KeyboardModifiers modifiers)
{
	const QList<Surface> surfaces = Surfaces();
	const int index = SurfaceAt(surfaces, global);
	if(index < 0)
		return;
	QWindow *window = surfaces[index].window;
	QWindowSystemInterface::handleMouseEvent<Delivery>(window, window->mapFromGlobal(global), global,
			buttons_after, button, press ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease, modifiers);
	last_mouse_global = global;
	has_last_mouse = true;
}

void McpUiTools::SendMouse(const QPointF &global, Qt::MouseButton button, int clicks, Qt::KeyboardModifiers modifiers, QString *target_name)
{
	const QList<Surface> surfaces = Surfaces();
	const int index = SurfaceAt(surfaces, global);
	if(target_name)
		*target_name = index >= 0 ? WindowDescription(surfaces[index].window) : QString();
	SendMouseMove(global, Qt::NoButton, modifiers);
	for(int i = 0; i < clicks; i++)
	{
		SendMouseButton(global, button, true, button, modifiers);
		SendMouseButton(global, button, false, Qt::NoButton, modifiers);
	}
}

bool McpUiTools::SendKeyCombination(const QString &combo, int repeat, QString *error)
{
	QStringList tokens;
	// Split on '+' but keep a literal trailing "+" key (e.g. "ctrl++").
	QString current;
	for(int i = 0; i < combo.size(); i++)
	{
		const QChar c = combo[i];
		if(c == QLatin1Char('+') && !current.trimmed().isEmpty())
		{
			tokens.append(current);
			current.clear();
		}
		else
			current.append(c);
	}
	if(!current.isEmpty())
		tokens.append(current);
	if(tokens.isEmpty())
	{
		*error = QStringLiteral("Empty key");
		return false;
	}

	Qt::KeyboardModifiers mods = Qt::NoModifier;
	for(int i = 0; i < tokens.size() - 1; i++)
	{
		const QString t = tokens[i].trimmed().toLower();
		if(t == QLatin1String("shift"))
			mods |= Qt::ShiftModifier;
		else if(t == QLatin1String("ctrl") || t == QLatin1String("control"))
			mods |= Qt::ControlModifier;
		else if(t == QLatin1String("alt") || t == QLatin1String("option"))
			mods |= Qt::AltModifier;
		else if(t == QLatin1String("meta") || t == QLatin1String("cmd") || t == QLatin1String("win") || t == QLatin1String("super"))
			mods |= Qt::MetaModifier;
		else
		{
			*error = QStringLiteral("Unknown modifier '%1'").arg(tokens[i]);
			return false;
		}
	}

	const QString key_name = CanonicalKeyToken(tokens.last());
	int key = 0;
	QString text;
	if(key_name.size() == 1)
	{
		const QChar c = key_name[0];
		key = c.toUpper().unicode();
		if(!(mods & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier)))
			text = (mods & Qt::ShiftModifier) ? QString(c.toUpper()) : QString(c);
	}
	else
	{
		const QKeySequence seq = QKeySequence::fromString(key_name, QKeySequence::PortableText);
		if(seq.count() != 1 || seq[0].key() == Qt::Key_unknown)
		{
			*error = QStringLiteral("Unknown key '%1'. Use names like Return, Escape, Tab, Space, Backspace, Up, Down, Left, Right, Home, End, PgUp, PgDown, F1-F12 or a single character.").arg(tokens.last());
			return false;
		}
		key = seq[0].key();
		mods |= seq[0].keyboardModifiers();
		if(key == Qt::Key_Space && !(mods & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier)))
			text = QStringLiteral(" ");
		else if((key == Qt::Key_Return || key == Qt::Key_Enter) && !(mods & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier)))
			text = QStringLiteral("\r");
		else if(key == Qt::Key_Tab && !(mods & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier)))
			text = QStringLiteral("\t");
	}

	const QList<Surface> surfaces = Surfaces();
	QWindow *target = KeyboardTarget(surfaces);
	if(!target)
	{
		*error = QStringLiteral("No window to receive keyboard input");
		return false;
	}
	for(int i = 0; i < qMax(1, repeat); i++)
	{
		QWindowSystemInterface::handleKeyEvent<Delivery>(target, QEvent::KeyPress, key, mods, text);
		QWindowSystemInterface::handleKeyEvent<Delivery>(target, QEvent::KeyRelease, key, mods, text);
		// Re-evaluate: the key may have opened or closed a window.
		target = KeyboardTarget(Surfaces());
		if(!target)
			break;
	}
	return true;
}

void McpUiTools::SendText(const QString &text)
{
	for(const QChar c : text)
	{
		QWindow *target = KeyboardTarget(Surfaces());
		if(!target)
			return;
		int key;
		QString key_text(c);
		Qt::KeyboardModifiers mods = Qt::NoModifier;
		if(c == QLatin1Char('\n') || c == QLatin1Char('\r'))
		{
			key = Qt::Key_Return;
			key_text = QStringLiteral("\r");
		}
		else if(c == QLatin1Char('\t'))
			key = Qt::Key_Tab;
		else
		{
			key = c.toUpper().unicode();
			if(c.isUpper())
				mods |= Qt::ShiftModifier;
		}
		QWindowSystemInterface::handleKeyEvent<Delivery>(target, QEvent::KeyPress, key, mods, key_text);
		QWindowSystemInterface::handleKeyEvent<Delivery>(target, QEvent::KeyRelease, key, mods, key_text);
	}
}

QQuickItem *McpUiTools::RootItem(const Surface &surface) const
{
	return surface.scene ? surface.scene->contentItem() : nullptr;
}

QJsonObject McpUiTools::DescribeItem(QQuickItem *item, const Surface &surface, const Mapping &mapping, const QString &id) const
{
	const QPointF scene_pos = item->mapToScene(QPointF(0, 0));
	const QRectF global_rect(QPointF(surface.global_rect.topLeft()) + scene_pos, QSizeF(item->width(), item->height()));
	const QRectF r = GlobalToImage(global_rect, mapping);
	QJsonObject o{
		{"id", id},
		{"type", CleanTypeName(item)},
		{"x", qRound(r.x())}, {"y", qRound(r.y())},
		{"width", qRound(r.width())}, {"height", qRound(r.height())},
		{"center_x", qRound(r.center().x())}, {"center_y", qRound(r.center().y())},
	};
	const QString text = ItemText(item);
	if(!text.isEmpty())
		o["text"] = text;
	if(!item->objectName().isEmpty())
		o["object_name"] = item->objectName();
	if(!item->isEnabled())
		o["enabled"] = false;
	if(item->hasActiveFocus())
		o["focused"] = true;
	if(item->property("checkable").toBool())
		o["checked"] = item->property("checked").toBool();
	const QVariant highlighted = item->property("highlighted");
	if(highlighted.isValid() && highlighted.toBool())
		o["highlighted"] = true;
	const QVariant value = item->property("value");
	if(value.isValid() && item->property("from").isValid() && item->property("to").isValid())
	{
		o["value"] = value.toDouble();
		o["from"] = item->property("from").toDouble();
		o["to"] = item->property("to").toDouble();
	}
	const QVariant current_index = item->property("currentIndex");
	if(current_index.isValid() && current_index.metaType().id() == QMetaType::Int)
		o["current_index"] = current_index.toInt();
	const QVariant count = item->property("count");
	if(count.isValid() && count.metaType().id() == QMetaType::Int && IsInteractiveType(CleanTypeName(item)))
		o["count"] = count.toInt();
	return o;
}

void McpUiTools::DumpTree(QQuickItem *item, const Surface &surface, int surface_index, const QString &path, int depth,
		const Mapping &mapping, const QJsonObject &options, QStringList *lines, QJsonArray *json, int *count) const
{
	const int max_depth = options.value("max_depth").toInt(60);
	const int max_nodes = options.value("max_nodes").toInt(1500);
	const bool include_hidden = options.value("include_hidden").toBool(false);
	const bool verbose = options.value("verbose").toBool(false);
	if(*count >= max_nodes || depth > max_depth)
		return;

	const bool visible = item->isVisible() && item->opacity() > 0.0;
	if(!visible && !include_hidden)
		return;

	const QString type = CleanTypeName(item);
	const QString text = ItemText(item);
	const QPointF scene_pos = item->mapToScene(QPointF(0, 0));
	const QRectF scene_rect(scene_pos, QSizeF(item->width(), item->height()));
	const bool on_screen = scene_rect.intersects(QRectF(QPointF(0, 0), QSizeF(surface.global_rect.size())));
	const bool has_size = item->width() > 0 && item->height() > 0;
	const bool interactive_only = options.value("interactive_only").toBool(false);
	const bool interesting = interactive_only
		? (IsInteractiveType(type) || item->hasActiveFocus() || item->activeFocusOnTab())
		: (verbose || !text.isEmpty() || !item->objectName().isEmpty() || item->hasActiveFocus() ||
			item->activeFocusOnTab() || IsInteractiveType(type));
	const bool emit_node = depth == 0 || (interesting && (include_hidden || (on_screen && has_size)));

	const QString id = QStringLiteral("%1/%2").arg(surface_index).arg(path);
	int child_depth = depth;
	if(emit_node)
	{
		(*count)++;
		child_depth = depth + 1;
		QJsonObject node = DescribeItem(item, surface, mapping, id);
		if(!visible)
			node["visible"] = false;
		if(!on_screen)
			node["offscreen"] = true;
		if(lines)
		{
			QString line = QString(depth * 2, QLatin1Char(' ')) + QStringLiteral("[%1] %2").arg(id, type);
			if(!text.isEmpty())
				line += QStringLiteral(" \"%1\"").arg(text);
			if(!item->objectName().isEmpty())
				line += QStringLiteral(" #%1").arg(item->objectName());
			line += QStringLiteral(" (%1,%2 %3x%4)").arg(node["x"].toInt()).arg(node["y"].toInt()).arg(node["width"].toInt()).arg(node["height"].toInt());
			QStringList flags;
			if(node.value("focused").toBool())
				flags << QStringLiteral("FOCUSED");
			if(node.contains("checked"))
				flags << (node["checked"].toBool() ? QStringLiteral("checked") : QStringLiteral("unchecked"));
			if(node.value("highlighted").toBool())
				flags << QStringLiteral("highlighted");
			if(node.contains("enabled"))
				flags << QStringLiteral("disabled");
			if(node.contains("value"))
				flags << QStringLiteral("value=%1 [%2..%3]").arg(node["value"].toDouble()).arg(node["from"].toDouble()).arg(node["to"].toDouble());
			if(node.contains("current_index"))
				flags << QStringLiteral("index=%1").arg(node["current_index"].toInt());
			if(node.contains("count"))
				flags << QStringLiteral("count=%1").arg(node["count"].toInt());
			if(!visible)
				flags << QStringLiteral("hidden");
			if(!on_screen)
				flags << QStringLiteral("offscreen");
			if(!flags.isEmpty())
				line += QLatin1Char(' ') + flags.join(QLatin1Char(' '));
			lines->append(line);
		}
		if(json)
			json->append(node);
	}

	QList<QQuickItem *> children = item->childItems();
	std::stable_sort(children.begin(), children.end(), [](QQuickItem *a, QQuickItem *b) { return a->z() < b->z(); });
	const QList<QQuickItem *> original = item->childItems();
	for(QQuickItem *child : children)
	{
		// Ids use the unsorted child index so they can be resolved again.
		const int index = original.indexOf(child);
		DumpTree(child, surface, surface_index, path.isEmpty() ? QString::number(index) : QStringLiteral("%1.%2").arg(path).arg(index),
				child_depth, mapping, options, lines, json, count);
	}
}

QQuickItem *McpUiTools::ResolveElementId(const QList<Surface> &surfaces, const QString &id, int *surface_index) const
{
	const qsizetype slash = id.indexOf(QLatin1Char('/'));
	if(slash <= 0)
		return nullptr;
	bool ok = false;
	const int index = id.left(slash).toInt(&ok);
	if(!ok || index < 0 || index >= surfaces.size())
		return nullptr;
	QQuickItem *item = RootItem(surfaces[index]);
	const QString path = id.mid(slash + 1);
	if(!path.isEmpty())
	{
		for(const QString &part : path.split(QLatin1Char('.')))
		{
			const int child = part.toInt(&ok);
			if(!ok || !item)
				return nullptr;
			const QList<QQuickItem *> children = item->childItems();
			if(child < 0 || child >= children.size())
				return nullptr;
			item = children[child];
		}
	}
	if(surface_index)
		*surface_index = index;
	return item;
}

QList<McpUiTools::ElementRef> McpUiTools::FindElements(const QList<Surface> &surfaces, const QJsonObject &query) const
{
	const QString text = query.value("text").toString();
	const QString type = query.value("type").toString();
	const QString object_name = query.value("object_name").toString();
	const bool exact = query.value("exact").toBool(false);
	const bool include_hidden = query.value("include_hidden").toBool(false);

	QList<ElementRef> exact_matches, matches;
	std::function<void(QQuickItem *, int, const QString &)> walk = [&](QQuickItem *item, int surface_index, const QString &path) {
		const bool visible = item->isVisible() && item->opacity() > 0.0;
		if(!visible && !include_hidden)
			return;
		const QString item_text = ItemText(item);
		const QString item_type = CleanTypeName(item);
		bool ok = true;
		bool is_exact = true;
		if(!text.isEmpty())
		{
			if(item_text.compare(text, Qt::CaseInsensitive) == 0)
				;
			else if(!exact && item_text.contains(text, Qt::CaseInsensitive))
				is_exact = false;
			else
				ok = false;
		}
		if(ok && !type.isEmpty() && !item_type.contains(type, Qt::CaseInsensitive))
			ok = false;
		if(ok && !object_name.isEmpty() && item->objectName() != object_name)
			ok = false;
		if(ok && (item->width() <= 0 || item->height() <= 0) && !include_hidden)
			ok = false;
		if(ok && (text.isEmpty() && type.isEmpty() && object_name.isEmpty()))
			ok = false;
		if(ok)
		{
			ElementRef ref{item, surface_index, QStringLiteral("%1/%2").arg(surface_index).arg(path)};
			(is_exact ? exact_matches : matches).append(ref);
		}
		const QList<QQuickItem *> children = item->childItems();
		for(int i = 0; i < children.size(); i++)
			walk(children[i], surface_index, path.isEmpty() ? QString::number(i) : QStringLiteral("%1.%2").arg(path).arg(i));
	};
	for(int i = 0; i < surfaces.size(); i++)
		if(QQuickItem *root = RootItem(surfaces[i]))
			walk(root, i, QString());

	QList<ElementRef> result = exact_matches + matches;
	// Prefer controls over the labels inside them, and topmost windows over the main window.
	std::stable_sort(result.begin(), result.end(), [](const ElementRef &a, const ElementRef &b) {
		const bool ia = IsInteractiveType(CleanTypeName(a.item)), ib = IsInteractiveType(CleanTypeName(b.item));
		if(ia != ib)
			return ia;
		return a.surface > b.surface;
	});
	return result;
}

QJsonObject McpUiTools::ScreenSummary() const
{
	static const QStringList generic = {
		"ListView", "GridView", "PathView", "TableView", "TreeView", "StackView", "SwipeView", "ScrollView",
		"WebEngineView", "Flickable", "ApplicationWindow",
	};
	auto is_screen_type = [](const QString &type) {
		return (type.endsWith(QLatin1String("View")) || type.endsWith(QLatin1String("Dialog")) ||
				type.endsWith(QLatin1String("Window")) || type.endsWith(QLatin1String("Popup")) ||
				type.endsWith(QLatin1String("Menu")) || type.endsWith(QLatin1String("Drawer"))) && !generic.contains(type);
	};

	const QList<Surface> surfaces = Surfaces();
	QStringList layers, tabs;
	QString screen;
	std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
		if(!item->isVisible() || item->opacity() <= 0.0)
			return;
		// A Popup/Dialog is drawn by an internal PopupItem whose QObject parent is the Popup itself.
		const QObject *described = item;
		QString type = CleanTypeName(item);
		if(type.contains(QLatin1String("PopupItem")) && item->parent())
		{
			described = item->parent();
			type = CleanTypeName(described);
		}
		if(is_screen_type(type) && item->width() > 0 && item->height() > 0)
		{
			QString name = type;
			const QString title = described->property("title").toString();
			if(!title.isEmpty())
				name += QStringLiteral(" \"%1\"").arg(title);
			if(!layers.contains(name))
				layers << name;
			screen = name;
		}
		if(type == QLatin1String("TabBar"))
		{
			if(auto *current = qvariant_cast<QQuickItem *>(item->property("currentItem")))
			{
				const QString tab = ItemText(current);
				if(!tab.isEmpty())
					tabs << tab;
			}
		}
		QList<QQuickItem *> children = item->childItems();
		std::stable_sort(children.begin(), children.end(), [](QQuickItem *a, QQuickItem *b) { return a->z() < b->z(); });
		for(QQuickItem *child : children)
			walk(child);
	};
	for(const Surface &s : surfaces)
	{
		// Separate windows (stream menu, settings windows, dialogs) are layers of their own.
		if(!s.is_main && s.window)
		{
			QString name = CleanTypeName(s.window);
			// A plain QML Window has no meaningful class name; fall back to its objectName/title.
			if(name.startsWith(QLatin1String("WindowQml")) || name == QLatin1String("Window") || name == QLatin1String("QWindow"))
				name = WindowDescription(s.window);
			if(!s.window->objectName().isEmpty() && name != s.window->objectName())
				name += QStringLiteral(" #%1").arg(s.window->objectName());
			if(!layers.contains(name))
				layers << name;
			screen = name;
		}
		if(QQuickItem *root = RootItem(s))
			walk(root);
	}

	QJsonObject summary{{"screen", screen.isEmpty() ? QStringLiteral("unknown") : screen}};
	if(layers.size() > 1)
		summary["open_layers"] = QJsonArray::fromStringList(layers);
	if(!tabs.isEmpty())
		summary["selected_tabs"] = QJsonArray::fromStringList(tabs);
	QWindow *focus_window = KeyboardTarget(surfaces);
	for(const Surface &s : surfaces)
	{
		if(s.window != focus_window || !s.scene || !s.scene->activeFocusItem())
			continue;
		QQuickItem *focus = s.scene->activeFocusItem();
		QString desc = CleanTypeName(focus);
		const QString text = ItemText(focus);
		if(!text.isEmpty())
			desc += QStringLiteral(" \"%1\"").arg(text);
		summary["keyboard_focus"] = desc;
	}
	return summary;
}

void McpUiTools::SelfCheck(const std::function<void(const QJsonArray &checks)> &done)
{
	auto check = [](const QString &name, const QString &status, const QString &detail, const QString &fix = QString()) {
		QJsonObject c{{"check", name}, {"status", status}, {"detail", detail}};
		if(!fix.isEmpty())
			c["fix"] = fix;
		return c;
	};

	QJsonArray checks;
	if(!main_window)
	{
		checks.append(check("window", "fail", "The main window is gone"));
		done(checks);
		return;
	}
	const QWindow::Visibility visibility = main_window->visibility();
	if(visibility == QWindow::Minimized || visibility == QWindow::Hidden)
		checks.append(check("window", "warn", "The window is minimized or hidden, so nothing is rendered",
				"window_control {action: restore}; take_screenshot also restores it automatically"));
	else if(!main_window->isExposed())
		checks.append(check("window", "warn", "The window is not exposed (covered by a lock screen or on an inactive desktop)",
				"Unlock the screen / switch to the desktop showing chiaki-ng"));
	else
		checks.append(check("window", "pass", QStringLiteral("Visible, %1x%2 px at device pixel ratio %3")
				.arg(qRound(main_window->width() * main_window->devicePixelRatio()))
				.arg(qRound(main_window->height() * main_window->devicePixelRatio()))
				.arg(main_window->devicePixelRatio())));

	const QList<Surface> surfaces = Surfaces();
	int nodes = 0;
	if(QQuickItem *root = RootItem(surfaces.first()))
	{
		QJsonArray json;
		DumpTree(root, surfaces.first(), 0, QString(), 0, ActiveMapping(), QJsonObject{{"max_nodes", 5000}}, nullptr, &json, &nodes);
	}
	checks.append(nodes > 1
		? check("ui_tree", "pass", QStringLiteral("%1 inspectable controls, %2 window(s)").arg(nodes).arg(surfaces.size()))
		: check("ui_tree", "warn", "No inspectable controls found in the main window", "Use screenshots and coordinates instead"));

	if(QGuiApplication::focusWindow())
		checks.append(check("keyboard", "pass", QStringLiteral("The app has keyboard focus (%1)").arg(WindowDescription(QGuiApplication::focusWindow()))));
	else
		checks.append(check("keyboard", "warn", "Another application has keyboard focus; press_key/type_text will try to bring chiaki-ng to the front first",
				"If the OS refuses to switch focus, window_control {action: activate} or ask the user to click the window once"));

	QPointer<McpUiTools> self(this);
	auto timer = QSharedPointer<QElapsedTimer>::create();
	timer->start();
	main_grabber([self, checks, timer, done, check](const QImage &frame, const QString &error) mutable {
		if(!self)
			return;
		if(frame.isNull())
		{
			checks.append(check("capture", "fail", QStringLiteral("Frame capture failed: %1").arg(error),
					"Make sure the window is visible and not minimized, then retry"));
		}
		else
		{
			const QImage small = frame.scaled(64, 36, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
			qint64 sum = 0;
			for(int y = 0; y < small.height(); y++)
				for(int x = 0; x < small.width(); x++)
					sum += small.constScanLine(y)[x];
			const double brightness = double(sum) / (small.width() * small.height());
			const QString detail = QStringLiteral("Captured %1x%2 in %3 ms, mean brightness %4")
				.arg(frame.width()).arg(frame.height()).arg(timer->elapsed()).arg(brightness, 0, 'f', 1);
			if(brightness < 1.0)
				checks.append(check("capture", "warn", detail + QStringLiteral(" (the picture is black)"),
						"Normal while a stream loads or the console is asleep; otherwise retry take_screenshot"));
			else
				checks.append(check("capture", "pass", detail));
		}
		done(checks);
	});
}

QString McpUiTools::ReadScreenText(int only_window) const
{
	const QList<Surface> surfaces = Surfaces();
	const Mapping mapping = ActiveMapping();
	QStringList out;
	for(int i = 0; i < surfaces.size(); i++)
	{
		if(only_window >= 0 && i != only_window)
			continue;
		QQuickItem *root = RootItem(surfaces[i]);
		if(!root)
			continue;
		QJsonArray nodes;
		int count = 0;
		DumpTree(root, surfaces[i], i, QString(), 0, mapping, QJsonObject{{"max_nodes", 2000}}, nullptr, &nodes, &count);
		struct Line
		{
			int x, y, h;
			QString text;
		};
		QList<Line> lines;
		QSet<QString> seen;
		for(const QJsonValue &v : nodes)
		{
			const QJsonObject n = v.toObject();
			if(!n.contains("text") || n.value("offscreen").toBool(false))
				continue;
			const QString key = QStringLiteral("%1|%2|%3").arg(n["text"].toString()).arg(n["center_x"].toInt()).arg(n["center_y"].toInt());
			if(seen.contains(key))
				continue;
			seen.insert(key);
			const QString type = n["type"].toString();
			QString text = n["text"].toString();
			if(IsInteractiveType(type))
			{
				QString state;
				if(n.contains("checked"))
					state = n["checked"].toBool() ? QStringLiteral(" [x]") : QStringLiteral(" [ ]");
				text = QStringLiteral("[%1%2] %3").arg(type, state, text);
			}
			if(n.value("focused").toBool(false))
				text = QStringLiteral("> ") + text + QStringLiteral("  <- focused");
			if(n.contains("enabled"))
				text += QStringLiteral(" (disabled)");
			lines.append({n["x"].toInt(), n["center_y"].toInt(), qMax(1, n["height"].toInt()), text});
		}
		// Reading order: rows top to bottom (items whose centers are close share a row), then left to right.
		std::stable_sort(lines.begin(), lines.end(), [](const Line &a, const Line &b) {
			const int tolerance = qMax(4, qMin(a.h, b.h) / 2);
			if(qAbs(a.y - b.y) > tolerance)
				return a.y < b.y;
			return a.x < b.x;
		});
		out << QStringLiteral("== Window %1: %2 (%3) ==").arg(i).arg(WindowDescription(surfaces[i].window), surfaces[i].kind);
		for(const Line &l : lines)
			out << l.text;
		if(lines.isEmpty())
			out << QStringLiteral("(no text)");
	}
	return out.join(QLatin1Char('\n'));
}

QString McpUiTools::SuggestElements(const QList<Surface> &surfaces, const QJsonObject &query) const
{
	const QString text = query.value("text").toString().toLower();
	const QString type = query.value("type").toString().toLower();
	QStringList words;
	for(const QString &w : text.split(QRegularExpression(QStringLiteral("\\W+")), Qt::SkipEmptyParts))
		if(w.size() >= 2)
			words << w;

	struct Candidate
	{
		int score;
		QString line;
		QString key; // text + center: a label inside a button is the same target
	};
	QList<Candidate> candidates;
	const Mapping mapping = ActiveMapping();
	for(int i = 0; i < surfaces.size(); i++)
	{
		QQuickItem *root = RootItem(surfaces[i]);
		if(!root)
			continue;
		QJsonArray nodes;
		int count = 0;
		DumpTree(root, surfaces[i], i, QString(), 0, mapping, QJsonObject{{"max_nodes", 800}}, nullptr, &nodes, &count);
		for(const QJsonValue &v : nodes)
		{
			const QJsonObject n = v.toObject();
			const QString node_text = n.value("text").toString();
			const QString node_type = n.value("type").toString();
			if(node_text.isEmpty() && !IsInteractiveType(node_type))
				continue;
			// Empty containers (lists, scroll areas) are rarely what a failed query meant.
			if(node_text.isEmpty() && (node_type.contains(QLatin1String("View")) || node_type.contains(QLatin1String("Flickable"))))
				continue;
			int score = IsInteractiveType(node_type) ? 1 : 0;
			const QString lower = node_text.toLower();
			for(const QString &w : words)
				if(lower.contains(w))
					score += 10;
			if(!text.isEmpty() && text.contains(lower) && !lower.isEmpty())
				score += 5;
			if(!type.isEmpty() && node_type.toLower().contains(type))
				score += 3;
			QString line = QStringLiteral("[%1] %2").arg(n["id"].toString(), node_type);
			if(!node_text.isEmpty())
				line += QStringLiteral(" \"%1\"").arg(node_text);
			line += QStringLiteral(" at (%1,%2)").arg(n["center_x"].toInt()).arg(n["center_y"].toInt());
			candidates.append({score, line, QStringLiteral("%1|%2|%3").arg(node_text).arg(n["center_x"].toInt()).arg(n["center_y"].toInt())});
		}
	}
	std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
	QStringList lines;
	QSet<QString> seen;
	for(const Candidate &c : candidates)
	{
		if(lines.size() >= 8)
			break;
		if(seen.contains(c.key))
			continue;
		seen.insert(c.key);
		lines << c.line;
	}
	if(lines.isEmpty())
		return QStringLiteral("No controls are visible (a stream picture has no controls: use the screenshot and controller_input).");
	return QStringLiteral("Visible controls that may be what you meant:\n%1\nPass one of these ids to click_element, "
			"or take_screenshot annotate=true to see them.").arg(lines.join(QLatin1Char('\n')));
}

QJsonObject McpUiTools::WindowInfo() const
{
	const QList<Surface> surfaces = Surfaces();
	const Mapping mapping = ActiveMapping();
	QJsonArray windows;
	for(int i = 0; i < surfaces.size(); i++)
	{
		const Surface &s = surfaces[i];
		const QRectF r = GlobalToImage(QRectF(s.global_rect), mapping);
		QJsonObject w{
			{"index", i},
			{"name", WindowDescription(s.window)},
			{"kind", s.kind},
			{"active", s.window && s.window->isActive()},
			{"accepts_input", s.accepts_input},
			{"rect_in_screenshot", QJsonObject{{"x", qRound(r.x())}, {"y", qRound(r.y())}, {"width", qRound(r.width())}, {"height", qRound(r.height())}}},
			{"logical_geometry", QJsonObject{{"x", s.global_rect.x()}, {"y", s.global_rect.y()}, {"width", s.global_rect.width()}, {"height", s.global_rect.height()}}},
		};
		if(s.scene && s.scene->activeFocusItem())
		{
			QQuickItem *focus = s.scene->activeFocusItem();
			QJsonObject f{{"type", CleanTypeName(focus)}};
			const QString text = ItemText(focus);
			if(!text.isEmpty())
				f["text"] = text;
			if(!focus->objectName().isEmpty())
				f["object_name"] = focus->objectName();
			w["focused_item"] = f;
		}
		windows.append(w);
	}

	QJsonObject info{{"location", ScreenSummary()}, {"windows", windows}};
	if(main_window)
	{
		QString state;
		switch(main_window->visibility())
		{
			case QWindow::FullScreen: state = QStringLiteral("fullscreen"); break;
			case QWindow::Maximized: state = QStringLiteral("maximized"); break;
			case QWindow::Minimized: state = QStringLiteral("minimized"); break;
			case QWindow::Hidden: state = QStringLiteral("hidden"); break;
			default: state = QStringLiteral("normal"); break;
		}
		info["main_window"] = QJsonObject{
			{"state", state},
			{"exposed", main_window->isExposed()},
			{"active", main_window->isActive()},
			{"device_pixel_ratio", main_window->devicePixelRatio()},
			{"pixel_width", qRound(main_window->width() * main_window->devicePixelRatio())},
			{"pixel_height", qRound(main_window->height() * main_window->devicePixelRatio())},
		};
		if(QScreen *screen = main_window->screen())
			info["screen"] = QJsonObject{
				{"name", screen->name()},
				{"logical_width", screen->geometry().width()},
				{"logical_height", screen->geometry().height()},
				{"refresh_rate", screen->refreshRate()},
			};
	}
	info["screenshot_coordinate_space"] = QJsonObject{
		{"from_last_screenshot", last_mapping.valid},
		{"image_width", mapping.image_size.width()},
		{"image_height", mapping.image_size.height()},
		{"image_pixels_per_logical_pixel", mapping.scale},
	};
	info["application_has_focus"] = QGuiApplication::focusWindow() != nullptr;
	return info;
}

void McpUiTools::TakeScreenshot(const QJsonObject &args, const McpServer::Responder &respond)
{
	Screenshot(args, respond);
}

void McpUiTools::Screenshot(const QJsonObject &args, const McpServer::Responder &respond)
{
	if(!main_window)
	{
		respond(McpServer::ErrorResult(QStringLiteral("The main window is gone")));
		return;
	}
	if(main_window->visibility() == QWindow::Minimized || !main_window->isVisible())
	{
		if(!args.value("restore_if_minimized").toBool(true))
		{
			respond(McpServer::ErrorResult(QStringLiteral("The window is minimized/hidden so nothing is being rendered. Call window_control with action=restore, or pass restore_if_minimized=true.")));
			return;
		}
		main_window->showNormal();
		main_window->raise();
		// Give the compositor a moment to expose the window and the renderer a frame.
		QPointer<McpUiTools> self(this);
		QJsonObject retry = args;
		retry["restore_if_minimized"] = false;
		QTimer::singleShot(400, this, [self, retry, respond]() {
			if(self)
				self->Screenshot(retry, respond);
		});
		return;
	}

	const QList<Surface> surfaces = Surfaces();
	QPointer<McpUiTools> self(this);
	main_grabber([self, surfaces, args, respond](const QImage &frame, const QString &error) {
		if(!self)
			return;
		if(frame.isNull())
		{
			respond(McpServer::ErrorResult(QStringLiteral("Could not capture the main window: %1").arg(error.isEmpty() ? QStringLiteral("unknown error") : error)));
			return;
		}
		self->ComposeAndReply(surfaces, frame, args, respond);
	});
}

QImage McpUiTools::ComposeCanvas(const QList<Surface> &surfaces, const QImage &main_frame, QRect *canvas_out, double *scale_out, QJsonArray *layers) const
{
	auto alive = [](const Surface &s) { return s.window && (s.is_main || s.window->isVisible()); };
	const QRect main_rect = surfaces.first().global_rect;
	QRect canvas = main_rect;
	for(const Surface &s : surfaces)
		if(alive(s))
			canvas = canvas.united(s.global_rect);

	// Use the real frame size to derive the scale so fractional DPI stays pixel exact.
	const double scale = main_rect.width() > 0 ? double(main_frame.width()) / main_rect.width() : MainDpr();
	const QPoint main_offset = QPoint(qRound((main_rect.x() - canvas.x()) * scale), qRound((main_rect.y() - canvas.y()) * scale));
	*canvas_out = canvas;
	*scale_out = scale;

	auto add_layer = [&](const Surface &s, const QRect &rect_px) {
		if(!layers)
			return;
		layers->append(QJsonObject{
			{"window", WindowDescription(s.window)},
			{"kind", s.kind},
			{"x", rect_px.x()}, {"y", rect_px.y()}, {"width", rect_px.width()}, {"height", rect_px.height()},
		});
	};

	int others = 0;
	for(int i = 1; i < surfaces.size(); i++)
		others += alive(surfaces[i]) ? 1 : 0;
	if(others == 0)
	{
		// Only the main window: the frame is the screenshot, pixel for pixel.
		add_layer(surfaces.first(), QRect(QPoint(0, 0), main_frame.size()));
		return main_frame.convertToFormat(QImage::Format_RGB32);
	}

	QSize canvas_size(qRound(canvas.width() * scale), qRound(canvas.height() * scale));
	canvas_size = canvas_size.expandedTo(QSize(main_offset.x() + main_frame.width(), main_offset.y() + main_frame.height()));
	QImage image(canvas_size, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter.setCompositionMode(QPainter::CompositionMode_Source);
	painter.drawImage(main_offset, main_frame.convertToFormat(QImage::Format_RGB32));
	add_layer(surfaces.first(), QRect(main_offset, main_frame.size()));
	painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
	for(int i : PaintOrder(surfaces))
	{
		const Surface &s = surfaces[i];
		if(s.is_main || !alive(s))
			continue;
		QImage layer;
		if(s.scene)
			layer = s.scene->grabWindow();
		else if(s.widget)
			layer = s.widget->grab().toImage();
		if(layer.isNull())
			continue;
		const QRect target(qRound((s.global_rect.x() - canvas.x()) * scale), qRound((s.global_rect.y() - canvas.y()) * scale),
				qRound(s.global_rect.width() * scale), qRound(s.global_rect.height() * scale));
		painter.drawImage(target, layer);
		add_layer(s, target);
	}
	return image;
}

void McpUiTools::GrabComposite(const std::function<void(const QImage &image, const QString &error)> &callback)
{
	const QList<Surface> surfaces = Surfaces();
	if(surfaces.isEmpty())
	{
		callback(QImage(), QStringLiteral("The main window is gone"));
		return;
	}
	QPointer<McpUiTools> self(this);
	main_grabber([self, surfaces, callback](const QImage &frame, const QString &error) {
		if(!self)
			return;
		if(frame.isNull() || !surfaces.first().window)
		{
			callback(QImage(), error.isEmpty() ? QStringLiteral("capture failed") : error);
			return;
		}
		QRect canvas;
		double scale = 1.0;
		callback(self->ComposeCanvas(surfaces, frame, &canvas, &scale, nullptr), QString());
	});
}

void McpUiTools::ComposeAndReply(const QList<Surface> &surfaces, const QImage &main_frame, const QJsonObject &args, const McpServer::Responder &respond)
{
	// Indices must stay aligned with Surfaces() for element ids, so closed windows are skipped, not removed.
	if(surfaces.isEmpty() || !surfaces.first().is_main || !surfaces.first().window)
	{
		respond(McpServer::ErrorResult(QStringLiteral("The main window disappeared during capture")));
		return;
	}
	const QRect main_rect = surfaces.first().global_rect;
	QRect canvas;
	double scale = 1.0;
	QJsonArray layers;
	QImage image = ComposeCanvas(surfaces, main_frame, &canvas, &scale, &layers);

	// Full fidelity copy on disk if requested: always lossless and never scaled.
	QString saved_path;
	QString save_path = args.value("save_path").toString();
	if(!save_path.isEmpty())
	{
		// Only ever write PNG files and never clobber something else that happens to be there.
		if(!save_path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
			save_path += QStringLiteral(".png");
		QFileInfo info(save_path);
		if(info.exists() && !info.isFile())
		{
			respond(McpServer::ErrorResult(QStringLiteral("save_path %1 exists and is not a file").arg(info.absoluteFilePath())));
			return;
		}
		QDir().mkpath(info.absolutePath());
		QImageWriter writer(info.absoluteFilePath(), "png");
		if(!writer.write(image))
		{
			respond(McpServer::ErrorResult(QStringLiteral("Failed to save screenshot to %1: %2").arg(info.absoluteFilePath(), writer.errorString())));
			return;
		}
		saved_path = info.absoluteFilePath();
	}

	// Zoom: a native resolution view of part of the last full screenshot, for reading small text
	// that the (possibly scaled) full screenshot cannot show. Click coordinates keep referring to
	// the full screenshot, so the mapping is intentionally left unchanged.
	if(args.value("region").isObject())
	{
		if(!last_mapping.valid)
		{
			respond(McpServer::ErrorResult(QStringLiteral("Take a full screenshot first: region is given in its pixel coordinates")));
			return;
		}
		const QJsonObject r = args.value("region").toObject();
		const QRectF wanted(r.value("x").toDouble(), r.value("y").toDouble(), r.value("width").toDouble(), r.value("height").toDouble());
		if(wanted.width() <= 0 || wanted.height() <= 0)
		{
			respond(McpServer::ErrorResult(QStringLiteral("region needs x, y and a positive width/height (pixels of the last full screenshot)")));
			return;
		}
		const QPointF g0 = ImageToGlobal(wanted.topLeft(), last_mapping);
		const QPointF g1 = ImageToGlobal(wanted.bottomRight(), last_mapping);
		const QPointF canvas_origin(canvas.topLeft());
		const QRect crop = QRectF((g0 - canvas_origin) * scale, (g1 - canvas_origin) * scale).toAlignedRect().intersected(image.rect());
		if(crop.isEmpty())
		{
			respond(McpServer::ErrorResult(QStringLiteral("region lies outside the app")));
			return;
		}
		// Report the rectangle actually returned (clipping changes origin and per-axis zoom).
		const QRectF crop_global(QPointF(canvas.topLeft()) + QPointF(crop.topLeft()) / scale, QSizeF(crop.size()) / scale);
		const QRectF shown = GlobalToImage(crop_global, last_mapping);
		QImage zoomed = image.copy(crop);
		const qint64 region_max_bytes = args.contains("max_bytes") ? qint64(args.value("max_bytes").toDouble()) : kDefaultMaxImageBytes;
		QByteArray region_data;
		QString region_mime = QStringLiteral("image/png");
		for(int attempt = 0; attempt < 6; attempt++)
		{
			QBuffer buffer(&region_data);
			region_data.clear();
			buffer.open(QIODevice::WriteOnly);
			QImageWriter writer(&buffer, region_mime == QLatin1String("image/png") ? "png" : "jpeg");
			if(region_mime == QLatin1String("image/png"))
				writer.setCompression(80);
			else
				writer.setQuality(92);
			writer.write(region_mime == QLatin1String("image/png") ? zoomed : zoomed.convertToFormat(QImage::Format_RGB32));
			if(region_max_bytes <= 0 || region_data.size() <= region_max_bytes)
				break;
			if(region_mime == QLatin1String("image/png"))
				region_mime = QStringLiteral("image/jpeg");
			else
				zoomed = zoomed.scaled(zoomed.size() * 0.8, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		}
		const double zoom_x = zoomed.width() / shown.width();
		const double zoom_y = zoomed.height() / shown.height();
		const QJsonObject region_meta{
			{"region_in_screenshot", QJsonObject{{"x", shown.x()}, {"y", shown.y()}, {"width", shown.width()}, {"height", shown.height()}}},
			{"image_width", zoomed.width()},
			{"image_height", zoomed.height()},
			{"zoom", zoom_x},
			{"zoom_y", zoom_y},
			{"mime_type", region_mime},
			{"coordinates", QStringLiteral("Zoomed view only. Keep clicking in the last full screenshot's coordinates: "
					"x = %1 + px / %2, y = %3 + py / %4").arg(shown.x(), 0, 'f', 1).arg(zoom_x, 0, 'f', 3).arg(shown.y(), 0, 'f', 1).arg(zoom_y, 0, 'f', 3)},
		};
		respond(QJsonObject{{"content", QJsonArray{McpServer::ImageContent(region_data, region_mime), McpServer::TextContent(ToJson(region_meta))}}});
		return;
	}

	// Set-of-marks: number every control on the returned image so a vision model can tie
	// what it sees to element ids. The saved PNG above stays untouched.
	QJsonArray marks;
	if(args.value("annotate").toBool(false))
	{
		Mapping full;
		full.valid = true;
		full.scale = scale;
		full.origin_offset = QPointF(canvas.topLeft() - main_rect.topLeft());
		marks = AnnotateImage(&image, surfaces, full);
	}

	// Optional downscale. The whole image is always kept: only scaled, never cropped.
	double downscale = 1.0;
	const int max_width = args.value("max_width").toInt(0);
	const int max_height = args.value("max_height").toInt(0);
	if(max_width > 0 && image.width() > max_width)
		downscale = qMin(downscale, double(max_width) / image.width());
	if(max_height > 0 && image.height() > max_height)
		downscale = qMin(downscale, double(max_height) / image.height());

	const QString format_arg = args.value("format").toString(QStringLiteral("auto")).toLower();
	const int quality = qBound(1, args.value("quality").toInt(92), 100);
	const qint64 max_bytes = args.contains("max_bytes") ? qint64(args.value("max_bytes").toDouble()) : kDefaultMaxImageBytes;

	auto encode = [](const QImage &img, const char *fmt, int q) {
		QByteArray data;
		QBuffer buffer(&data);
		buffer.open(QIODevice::WriteOnly);
		QImageWriter writer(&buffer, fmt);
		if(qstrcmp(fmt, "png") == 0)
			writer.setCompression(80); // Qt scale 0-100, maps to zlib level ~8
		else
			writer.setQuality(q);
		writer.write(qstrcmp(fmt, "jpeg") == 0 ? img.convertToFormat(QImage::Format_RGB32) : img);
		return data;
	};

	QImage out = image;
	QByteArray data;
	QString mime;
	QStringList notes;
	for(int attempt = 0; attempt < 8; attempt++)
	{
		if(downscale < 1.0)
			out = image.scaled(qMax(1, qRound(image.width() * downscale)), qMax(1, qRound(image.height() * downscale)),
					Qt::KeepAspectRatio, Qt::SmoothTransformation);
		else
			out = image;

		if(format_arg == QLatin1String("jpeg") || format_arg == QLatin1String("jpg"))
		{
			data = encode(out, "jpeg", quality);
			mime = QStringLiteral("image/jpeg");
		}
		else
		{
			data = encode(out, "png", 0);
			mime = QStringLiteral("image/png");
			if(format_arg == QLatin1String("auto") && max_bytes > 0 && data.size() > max_bytes)
			{
				data = encode(out, "jpeg", quality);
				mime = QStringLiteral("image/jpeg");
				if(attempt == 0)
					notes << QStringLiteral("PNG exceeded max_bytes, sent as JPEG (quality %1) at the same resolution").arg(quality);
			}
		}
		if(max_bytes <= 0 || data.size() <= max_bytes)
			break;
		downscale *= std::sqrt(double(max_bytes) / data.size()) * 0.95;
		if(attempt == 0)
			notes << QStringLiteral("Image exceeded max_bytes and was scaled down (whole frame kept, nothing cropped)");
	}

	Mapping mapping;
	mapping.valid = true;
	mapping.scale = scale * (double(out.width()) / image.width());
	mapping.origin_offset = QPointF(canvas.topLeft() - main_rect.topLeft());
	mapping.image_size = out.size();
	last_mapping = mapping;

	const double layer_scale = double(out.width()) / image.width();
	if(layer_scale != 1.0)
	{
		for(int i = 0; i < layers.size(); i++)
		{
			QJsonObject l = layers[i].toObject();
			for(const char *k : {"x", "y", "width", "height"})
				l[k] = qRound(l[k].toInt() * layer_scale);
			layers[i] = l;
		}
		for(int i = 0; i < marks.size(); i++)
		{
			QJsonObject m = marks[i].toObject();
			m["center_x"] = qRound(m["center_x"].toInt() * layer_scale);
			m["center_y"] = qRound(m["center_y"].toInt() * layer_scale);
			marks[i] = m;
		}
	}

	QJsonObject meta{
		{"image_width", out.width()},
		{"image_height", out.height()},
		{"full_resolution_width", image.width()},
		{"full_resolution_height", image.height()},
		{"mime_type", mime},
		{"bytes", data.size()},
		{"layers", layers},
		{"image_pixels_per_logical_pixel", mapping.scale},
		{"coordinates", QStringLiteral("Pass x/y in this image's pixel space to click/scroll/drag (coordinate_space=screenshot, the default).")},
	};
	if(!saved_path.isEmpty())
		meta["saved_full_resolution_png"] = saved_path;
	if(!notes.isEmpty())
		meta["notes"] = QJsonArray::fromStringList(notes);
	meta["location"] = ScreenSummary();
	if(!marks.isEmpty())
	{
		meta["marks"] = marks;
		meta["marks_usage"] = QStringLiteral("Numbered boxes on the image are controls: pass a mark's id to click_element/focus_element, or click its center.");
	}
	if(QQuickWindow *scene = main_scene)
		if(QQuickItem *focus = scene->activeFocusItem())
		{
			QString focus_desc = CleanTypeName(focus);
			const QString text = ItemText(focus);
			if(!text.isEmpty())
				focus_desc += QStringLiteral(" \"%1\"").arg(text);
			meta["focused_item"] = focus_desc;
		}

	QJsonArray content{McpServer::ImageContent(data, mime), McpServer::TextContent(ToJson(meta))};
	respond(QJsonObject{{"content", content}});
}

QJsonArray McpUiTools::AnnotateImage(QImage *image, const QList<Surface> &surfaces, const Mapping &mapping) const
{
	struct Mark
	{
		QJsonObject node;
		QRect rect;
		bool interactive;
	};
	QList<Mark> candidates;
	const QJsonObject options{{"max_nodes", 600}};
	for(int i = 0; i < surfaces.size(); i++)
	{
		QQuickItem *root = RootItem(surfaces[i]);
		if(!root)
			continue;
		QJsonArray nodes;
		int count = 0;
		DumpTree(root, surfaces[i], i, QString(), 0, mapping, options, nullptr, &nodes, &count);
		for(const QJsonValue &v : nodes)
		{
			const QJsonObject n = v.toObject();
			const QRect r(n["x"].toInt(), n["y"].toInt(), n["width"].toInt(), n["height"].toInt());
			const QString type = n["type"].toString();
			if(r.width() < 6 || r.height() < 6 || n["id"].toString().endsWith(QLatin1Char('/')))
				continue;
			// Whole-window containers would box everything.
			if(type == QLatin1String("ContentItem") || type == QLatin1String("RootItem") || type.contains(QLatin1String("Overlay")))
				continue;
			const bool interactive = IsInteractiveType(type) && !type.contains(QLatin1String("View")) && !type.contains(QLatin1String("Flickable"));
			if(!interactive && !n.contains("text"))
				continue;
			candidates.append({n, r.intersected(image->rect()), interactive});
		}
	}
	std::stable_sort(candidates.begin(), candidates.end(), [](const Mark &a, const Mark &b) { return a.interactive && !b.interactive; });

	QList<Mark> chosen;
	for(const Mark &c : candidates)
	{
		if(c.rect.isEmpty())
			continue;
		bool redundant = false;
		for(const Mark &o : chosen)
		{
			// A label inside a button is the same target, and near identical rects are duplicates.
			// Parts of a control (indicator, handle, label) are the control itself; containers keep their children.
			const QString o_type = o.node["type"].toString();
			const bool o_container = o_type.contains(QLatin1String("Popup")) || o_type.contains(QLatin1String("Dialog")) ||
				o_type.contains(QLatin1String("Drawer")) || o_type.contains(QLatin1String("Menu")) || o_type.contains(QLatin1String("Delegate"));
			if(!o_container && c.node["id"].toString().startsWith(o.node["id"].toString() + QLatin1Char('.')))
			{
				redundant = true;
				break;
			}
			const QRect overlap = o.rect.intersected(c.rect);
			const double c_area = double(c.rect.width()) * c.rect.height();
			const double o_area = double(o.rect.width()) * o.rect.height();
			if((o.interactive && !c.interactive && o.rect.contains(c.rect.center())) ||
					(double(overlap.width()) * overlap.height() > 0.85 * c_area && o_area < 1.5 * c_area))
			{
				redundant = true;
				break;
			}
		}
		if(!redundant)
			chosen.append(c);
		if(chosen.size() >= 150)
			break;
	}

	QJsonArray marks;
	QPainter painter(image);
	painter.setRenderHint(QPainter::Antialiasing, true);
	QFont font = painter.font();
	font.setBold(true);
	font.setPixelSize(qMax(11, qRound(12 * mapping.scale)));
	painter.setFont(font);
	const QFontMetrics metrics(font);
	const int pen_width = qMax(2, qRound(mapping.scale * 1.5));
	QList<QRect> placed_tags;
	for(int i = 0; i < chosen.size(); i++)
	{
		const Mark &m = chosen[i];
		const QColor color = m.interactive ? QColor(255, 0, 200) : QColor(0, 200, 255);
		painter.setPen(QPen(color, pen_width));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(m.rect);
		const QString label = QString::number(i + 1);
		const QSize tag_size(metrics.horizontalAdvance(label) + 6, metrics.height() + 2);
		// Prefer spots outside the box so the control's own text stays readable, and never cover another tag.
		const QPoint spots[] = {
			m.rect.topLeft() - QPoint(0, tag_size.height()),
			QPoint(m.rect.left() - tag_size.width(), m.rect.top()),
			QPoint(m.rect.right() - tag_size.width() + 1, m.rect.top() - tag_size.height()),
			QPoint(m.rect.left(), m.rect.bottom() + 1),
			QPoint(m.rect.right() + 1, m.rect.top()),
			m.rect.topLeft(),
		};
		QRect tag(spots[0], tag_size);
		for(const QPoint &spot : spots)
		{
			const QRect candidate(spot, tag_size);
			if(!image->rect().contains(candidate))
				continue;
			bool collides = false;
			for(const QRect &other : placed_tags)
				if(other.intersects(candidate))
				{
					collides = true;
					break;
				}
			// Also keep clear of the other controls so their text stays readable.
			for(int j = 0; j < chosen.size() && !collides; j++)
				if(j != i && chosen[j].rect.intersects(candidate) && !chosen[j].rect.contains(m.rect))
					collides = true;
			tag = candidate;
			if(!collides)
				break;
		}
		if(!image->rect().contains(tag))
			tag.moveTopLeft(m.rect.topLeft());
		placed_tags.append(tag);
		painter.fillRect(tag, color);
		painter.setPen(Qt::black);
		painter.drawText(tag, Qt::AlignCenter, label);

		QJsonObject entry{
			{"mark", i + 1},
			{"id", m.node["id"]},
			{"type", m.node["type"]},
			{"center_x", m.node["center_x"]},
			{"center_y", m.node["center_y"]},
		};
		if(m.node.contains("text"))
			entry["text"] = m.node["text"];
		if(m.node.value("focused").toBool())
			entry["focused"] = true;
		if(m.node.contains("checked"))
			entry["checked"] = m.node["checked"];
		marks.append(entry);
	}
	return marks;
}

void McpUiTools::WaitFor(const QJsonObject &args, const QImage &baseline_frame, const McpServer::Responder &respond)
{
	QPointer<McpUiTools> self(this);
		const QString until = args.value("until").toString();
		const bool element_condition = until == QLatin1String("element") || until == QLatin1String("element_gone");
		const bool screen_condition = until == QLatin1String("screen_changed") || until == QLatin1String("screen_stable");
		if(!element_condition && !screen_condition)
		{
			respond(McpServer::ErrorResult(QStringLiteral("until must be element, element_gone, screen_changed or screen_stable")));
			return;
		}
		if(element_condition && args.value("text").toString().isEmpty() && args.value("type").toString().isEmpty() &&
				args.value("object_name").toString().isEmpty())
		{
			respond(McpServer::ErrorResult(QStringLiteral("Element conditions need text, type or object_name")));
			return;
		}

		struct WaitState
		{
			QElapsedTimer elapsed;
			QImage baseline;
			QImage previous;
			int stable_polls = 0;
			int polls = 0;
		};
		auto state = QSharedPointer<WaitState>::create();
		state->elapsed.start();
		const int timeout_ms = qBound(0, args.value("timeout_ms").toInt(10000), 60000);
		const int interval_ms = qBound(50, args.value("interval_ms").toInt(300), 5000);
		const double threshold = args.value("threshold").toDouble(2.0);

		auto poll = QSharedPointer<std::function<void()>>::create();
		auto finish = [self, args, state, poll](bool met, const QString &detail, const McpServer::Responder &respond) {
			// The poll closure references itself; release it once this event finishes.
			QTimer::singleShot(0, QCoreApplication::instance(), [poll]() { *poll = nullptr; });
			const QJsonObject summary{
				{"condition_met", met},
				{"timed_out", !met},
				{"elapsed_ms", qint64(state->elapsed.elapsed())},
				{"detail", detail},
			};
			const QJsonObject text_result = McpServer::TextResult(ToJson(summary));
			if(!self || !args.value("screenshot").toBool(true))
			{
				respond(text_result);
				return;
			}
			self->Screenshot(QJsonObject{{"annotate", args.value("annotate").toBool(false)}, {"restore_if_minimized", false}}, [text_result, respond](const QJsonObject &shot) {
				QJsonArray content = text_result.value("content").toArray();
				if(!shot.value("isError").toBool(false))
					for(const QJsonValue &c : shot.value("content").toArray())
						content.append(c);
				respond(QJsonObject{{"content", content}});
			});
		};

		// Grayscale thumbnails are cheap to compare. The score is the largest mean difference of a
		// 16x9 grid of blocks, so a small change (one label) counts as much as a full screen fade.
		auto thumbnail = [](const QImage &frame) {
			return frame.scaled(256, 144, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
		};
		auto difference = [](const QImage &a, const QImage &b) {
			if(a.size() != b.size())
				return 255.0;
			const int bw = a.width() / 16, bh = a.height() / 9;
			double worst = 0.0;
			for(int by = 0; by < 9; by++)
			{
				for(int bx = 0; bx < 16; bx++)
				{
					qint64 sum = 0;
					for(int y = by * bh; y < (by + 1) * bh; y++)
					{
						const uchar *pa = a.constScanLine(y), *pb = b.constScanLine(y);
						for(int x = bx * bw; x < (bx + 1) * bw; x++)
							sum += qAbs(int(pa[x]) - int(pb[x]));
					}
					worst = qMax(worst, double(sum) / (bw * bh));
				}
			}
			return worst;
		};

		if(!baseline_frame.isNull())
		{
			state->baseline = thumbnail(baseline_frame);
			state->previous = state->baseline;
		}

		*poll = [self, args, state, until, timeout_ms, interval_ms, threshold, finish, thumbnail, difference, poll, respond]() {
			if(!self)
				return;
			state->polls++;
			auto schedule_next = [self, state, timeout_ms, interval_ms, poll, finish, respond](const QString &timeout_detail) {
				if(state->elapsed.elapsed() >= timeout_ms)
				{
					finish(false, timeout_detail, respond);
					return;
				}
				QTimer::singleShot(interval_ms, self.data(), [poll]() { (*poll)(); });
			};

			if(until == QLatin1String("element") || until == QLatin1String("element_gone"))
			{
				const QList<ElementRef> found = self->FindElements(self->Surfaces(), args);
				const bool present = !found.isEmpty();
				if(until == QLatin1String("element") && present)
				{
					const QList<Surface> surfaces = self->Surfaces();
					const ElementRef &ref = found.first();
					QString desc;
					if(ref.item && ref.surface < surfaces.size())
						desc = ToJson(self->DescribeItem(ref.item, surfaces[ref.surface], self->ActiveMapping(), ref.id));
					finish(true, QStringLiteral("Element appeared: %1").arg(desc), respond);
					return;
				}
				if(until == QLatin1String("element_gone") && !present)
				{
					finish(true, QStringLiteral("Element is gone"), respond);
					return;
				}
				schedule_next(until == QLatin1String("element") ? QStringLiteral("Element did not appear") : QStringLiteral("Element is still there"));
				return;
			}

			self->GrabComposite([self, state, until, threshold, finish, thumbnail, difference, schedule_next, respond](const QImage &frame, const QString &error) {
				if(!self)
					return;
				if(frame.isNull())
				{
					finish(false, QStringLiteral("Could not capture the screen: %1").arg(error), respond);
					return;
				}
				const QImage thumb = thumbnail(frame);
				if(state->baseline.isNull())
				{
					state->baseline = thumb;
					state->previous = thumb;
					schedule_next(QStringLiteral("Timed out"));
					return;
				}
				if(until == QLatin1String("screen_changed"))
				{
					const double diff = difference(state->baseline, thumb);
					if(diff >= threshold)
					{
						finish(true, QStringLiteral("Screen changed (region difference %1)").arg(diff, 0, 'f', 1), respond);
						return;
					}
					schedule_next(QStringLiteral("Screen did not change (last difference %1)").arg(diff, 0, 'f', 1));
					return;
				}
				const double diff = difference(state->previous, thumb);
				state->previous = thumb;
				state->stable_polls = diff < threshold ? state->stable_polls + 1 : 0;
				if(state->stable_polls >= 2)
				{
					finish(true, QStringLiteral("Screen is stable"), respond);
					return;
				}
				schedule_next(QStringLiteral("Screen kept changing (last difference %1); it may be animated").arg(diff, 0, 'f', 1));
			});
		};
		(*poll)();
}

void McpUiTools::RunWithExpectation(const McpServer::Handler &action, const QJsonObject &args, const McpServer::Responder &respond)
{
	QJsonObject expect = args.value("expect").toObject();
	if(!expect.contains("timeout_ms"))
		expect["timeout_ms"] = 5000;
	expect["screenshot"] = args.value("screenshot_after").toBool(true);
	expect["annotate"] = args.value("annotate_screenshot").toBool(false);

	QPointer<McpUiTools> self(this);
	auto run = [self, action, args, expect, respond](const QImage &baseline) {
		if(!self)
			return;
		action(args, [self, expect, baseline, respond](const QJsonObject &result) {
			if(!self || result.value("isError").toBool(false))
			{
				respond(result);
				return;
			}
			self->WaitFor(expect, baseline, [result, respond](const QJsonObject &verification) {
				const QJsonArray verification_content = verification.value("content").toArray();
				const QString summary = verification_content.isEmpty() ? QString() : verification_content.first().toObject().value("text").toString();
				const bool met = QJsonDocument::fromJson(summary.toUtf8()).object().value("condition_met").toBool(false);
				QJsonArray content = result.value("content").toArray();
				if(verification.value("isError").toBool(false))
					content.append(McpServer::TextContent(QStringLiteral("Could not verify: %1").arg(summary)));
				else
				{
					content.append(McpServer::TextContent(met
						? QStringLiteral("Expectation met.")
						: QStringLiteral("EXPECTATION NOT MET: the action ran but the expected result did not happen. "
							"Look at the screenshot and try another approach (see the details below).")));
					for(const QJsonValue &c : verification_content)
						content.append(c);
				}
				QJsonObject merged{{"content", content}};
				if(!met)
					merged["isError"] = true;
				respond(merged);
			});
		});
	};

	if(expect.value("until").toString() == QLatin1String("screen_changed"))
	{
		// Capture "before" so a change made instantly by the action still counts. Without it the
		// check would be meaningless, so fail instead of silently using an after-action baseline.
		GrabComposite([run, respond](const QImage &frame, const QString &error) {
			if(frame.isNull())
			{
				respond(McpServer::ErrorResult(QStringLiteral("Could not capture the screen before the action (%1); the action was not run. "
						"Run mcp_self_check, or retry without expect.").arg(error)));
				return;
			}
			run(frame);
		});
		return;
	}
	run(QImage());
}

void McpUiTools::EnableScreenshotAfter(McpServer *server, const QStringList &tool_names)
{
	QPointer<McpUiTools> self(this);
	for(McpServer::Tool tool : server->Tools())
	{
		if(!tool_names.contains(tool.name))
			continue;
		QJsonObject properties = tool.input_schema.value("properties").toObject();
		properties["screenshot_after"] = Prop("boolean",
				"Also return a screenshot taken after the action, in the same response (saves a round trip)");
		properties["screenshot_delay_ms"] = Prop("integer",
				"Wait this long before that screenshot so the UI or console can react (default 500)");
		properties["annotate_screenshot"] = Prop("boolean", "Draw numbered element marks on that screenshot");
		properties["expect"] = QJsonObject{
			{"type", "object"},
			{"description", "Verify the action worked in the same call: waits (like wait_for) until the condition holds and "
				"reports an error if it never does. screen_changed compares against the screen from just before the action. "
				"Example: {\"until\": \"element\", \"text\": \"Settings\"} or {\"until\": \"screen_changed\"}."},
			{"properties", QJsonObject{
				{"until", EnumProp({"element", "element_gone", "screen_changed", "screen_stable"}, "Condition")},
				{"text", Prop("string", "Element text")},
				{"type", Prop("string", "Element type")},
				{"object_name", Prop("string", "Element objectName")},
				{"exact", Prop("boolean", "Whole text must match")},
				{"timeout_ms", Prop("integer", "Default 5000")},
				{"threshold", Prop("number", "Screen change threshold (default 2)")},
			}},
			{"required", QJsonArray{"until"}},
		};
		tool.input_schema["properties"] = properties;

		const McpServer::Handler original = tool.handler;
		tool.handler = [self, original](const QJsonObject &args, const McpServer::Responder &respond) {
			if(self && args.value("expect").isObject())
			{
				self->RunWithExpectation(original, args, respond);
				return;
			}
			if(!args.value("screenshot_after").toBool(false))
			{
				original(args, respond);
				return;
			}
			original(args, [self, args, respond](const QJsonObject &result) {
				if(!self || result.value("isError").toBool(false))
				{
					respond(result);
					return;
				}
				const int delay = qBound(0, args.value("screenshot_delay_ms").toInt(500), 10000);
				QTimer::singleShot(delay, self.data(), [self, args, result, respond]() {
					if(!self)
					{
						respond(result);
						return;
					}
					const QJsonObject shot_args{{"annotate", args.value("annotate_screenshot").toBool(false)}, {"restore_if_minimized", false}};
					self->Screenshot(shot_args, [result, respond](const QJsonObject &shot) {
						QJsonObject merged = result;
						QJsonArray content = result.value("content").toArray();
						if(shot.value("isError").toBool(false))
							content.append(McpServer::TextContent(QStringLiteral("(screenshot after the action failed: %1)")
									.arg(shot.value("content").toArray().first().toObject().value("text").toString())));
						else
							for(const QJsonValue &c : shot.value("content").toArray())
								content.append(c);
						merged["content"] = content;
						respond(merged);
					});
				});
			});
		};
		server->AddTool(tool);
	}
}

void McpUiTools::RegisterTools(McpServer *server)
{
	QPointer<McpUiTools> self(this);
	auto guard = [self](const std::function<void(const QJsonObject &, const McpServer::Responder &)> &fn) {
		return [self, fn](const QJsonObject &args, const McpServer::Responder &respond) {
			if(!self || !self->main_window)
			{
				respond(McpServer::ErrorResult(QStringLiteral("The application window is not available")));
				return;
			}
			fn(args, respond);
		};
	};

	server->AddTool({
		"take_screenshot",
		"Take screenshot",
		"Capture exactly what the app shows: the full main window at native resolution (stream video, "
		"UI and overlays as composited on screen) plus every other visible app window (dialogs, stream menu, "
		"stats overlay), never cropped. Coordinates of this image are what the input tools expect by default. "
		"If the image would be too large for the client it is re-encoded as JPEG and/or scaled down as a whole; "
		"use save_path to also get a lossless full resolution PNG on disk.",
		Schema({
			{"format", EnumProp({"auto", "png", "jpeg"}, "auto (default): PNG, falling back to JPEG when bigger than max_bytes")},
			{"quality", Prop("integer", "JPEG quality 1-100 (default 92)")},
			{"max_width", Prop("integer", "Scale the whole image down to at most this width (never crops)")},
			{"max_height", Prop("integer", "Scale the whole image down to at most this height (never crops)")},
			{"max_bytes", Prop("integer", "Maximum encoded size in bytes, 0 = unlimited (default 3584000)")},
			{"save_path", Prop("string", "Also write a lossless full resolution PNG to this path")},
			{"restore_if_minimized", Prop("boolean", "Restore the window first when it is minimized (default true)")},
			{"annotate", Prop("boolean", "Draw numbered boxes on every control and list their ids (set-of-marks); the easiest way to find what to click")},
			{"region", QJsonObject{
				{"type", "object"},
				{"description", "Zoom: return only this rectangle (pixels of the LAST full screenshot) at full native resolution, "
					"to read small text a scaled-down screenshot cannot show. Does not change click coordinates."},
				{"properties", QJsonObject{{"x", Prop("number", "Left")}, {"y", Prop("number", "Top")},
					{"width", Prop("number", "Width")}, {"height", Prop("number", "Height")}}},
				{"required", QJsonArray{"x", "y", "width", "height"}},
			}},
		}),
		true,
		guard([self](const QJsonObject &args, const McpServer::Responder &respond) { self->Screenshot(args, respond); }),
	});

	server->AddTool({
		"get_window_info",
		"Get window info",
		"Describe the app's visible windows (with their rectangles in screenshot coordinates), the main window "
		"state, DPI scale, screen, and which control has keyboard focus.",
		Schema({}),
		true,
		guard([self](const QJsonObject &, const McpServer::Responder &respond) {
			respond(McpServer::TextResult(ToJson(self->WindowInfo())));
		}),
	});

	server->AddTool({
		"get_ui_tree",
		"Get UI tree",
		"List the app's UI controls (buttons, text, fields, lists, dialogs) with ids, text, state and bounding "
		"boxes in screenshot coordinates. FOCUSED marks the control that receives keyboard/controller navigation. "
		"Ids can be passed to click_element/focus_element; they change when the UI changes, so refresh after navigating.",
		Schema({
			{"format", EnumProp({"text", "json"}, "text (default, compact outline) or json")},
			{"window", Prop("integer", "Only this window index (see get_window_info)")},
			{"max_depth", Prop("integer", "Maximum tree depth (default 60)")},
			{"max_nodes", Prop("integer", "Maximum number of nodes (default 1500)")},
			{"include_hidden", Prop("boolean", "Include invisible and off-screen items (default false)")},
			{"verbose", Prop("boolean", "Include purely structural items without text or interaction (default false)")},
			{"root", Prop("string", "Only the subtree under this element id (e.g. a dialog or settings page from a previous call); saves tokens")},
			{"interactive_only", Prop("boolean", "Only controls you can act on (buttons, fields, lists, ...), no plain labels")},
		}),
		true,
		guard([self](const QJsonObject &args, const McpServer::Responder &respond) {
			const QList<Surface> surfaces = self->Surfaces();
			const Mapping mapping = self->ActiveMapping();
			const bool json_format = args.value("format").toString() == QLatin1String("json");
			const int only = args.contains("window") ? args.value("window").toInt() : -1;
			QStringList lines;
			QJsonArray json;
			int count = 0;
			if(args.contains("root"))
			{
				const QString root_id = args.value("root").toString();
				int surface_index = -1;
				QQuickItem *root = self->ResolveElementId(surfaces, root_id, &surface_index);
				if(!root)
				{
					respond(McpServer::ErrorResult(QStringLiteral("No element with id %1. %2").arg(root_id, self->SuggestElements(surfaces, QJsonObject()))));
					return;
				}
				const QString path = root_id.mid(root_id.indexOf(QLatin1Char('/')) + 1);
				QJsonArray nodes;
				self->DumpTree(root, surfaces[surface_index], surface_index, path, 0, mapping, args,
						json_format ? nullptr : &lines, json_format ? &nodes : nullptr, &count);
				if(json_format)
					respond(McpServer::TextResult(QString::fromUtf8(QJsonDocument(nodes).toJson(QJsonDocument::Compact))));
				else
					respond(McpServer::TextResult(lines.join(QLatin1Char('\n'))));
				return;
			}
			for(int i = 0; i < surfaces.size(); i++)
			{
				if(only >= 0 && i != only)
					continue;
				QQuickItem *root = self->RootItem(surfaces[i]);
				const QRectF r = self->GlobalToImage(QRectF(surfaces[i].global_rect), mapping);
				const QString header = QStringLiteral("Window %1: %2 (%3) at (%4,%5 %6x%7)%8")
					.arg(i).arg(WindowDescription(surfaces[i].window), surfaces[i].kind)
					.arg(qRound(r.x())).arg(qRound(r.y())).arg(qRound(r.width())).arg(qRound(r.height()))
					.arg(surfaces[i].window && surfaces[i].window->isActive() ? QStringLiteral(" ACTIVE") : QString());
				if(!root)
				{
					lines << header + QStringLiteral(" - no inspectable controls");
					continue;
				}
				lines << header;
				QJsonArray window_nodes;
				self->DumpTree(root, surfaces[i], i, QString(), 0, mapping, args, json_format ? nullptr : &lines,
						json_format ? &window_nodes : nullptr, &count);
				if(json_format)
					json.append(QJsonObject{{"window", i}, {"name", WindowDescription(surfaces[i].window)}, {"kind", surfaces[i].kind}, {"elements", window_nodes}});
			}
			if(count >= args.value("max_nodes").toInt(1500))
				lines << QStringLiteral("... truncated at max_nodes");
			if(json_format)
				respond(McpServer::TextResult(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact))));
			else
				respond(McpServer::TextResult(lines.join(QLatin1Char('\n'))));
		}),
	});

	const QJsonObject element_query{
		{"id", Prop("string", "Element id from get_ui_tree/find_ui_elements, e.g. \"0/1.3.2\"")},
		{"text", Prop("string", "Match the visible text (case-insensitive, substring unless exact=true)")},
		{"type", Prop("string", "Match the control type, e.g. Button, CheckBox, TextField")},
		{"object_name", Prop("string", "Match the QML objectName exactly")},
		{"exact", Prop("boolean", "Require the whole text to match")},
		{"index", Prop("integer", "Which match to use when several match (default 0, best match first)")},
	};

	server->AddTool({
		"read_screen_text",
		"Read screen text",
		"All visible text of the app in reading order (top to bottom, left to right), with controls tagged like "
		"[Button] Connect, check states and the focused control marked. Much cheaper than a screenshot for reading "
		"menus, dialogs, errors and settings values. Does not see inside the console's video picture.",
		Schema({{"window", Prop("integer", "Only this window index (see get_window_info)")}}),
		true,
		guard([self](const QJsonObject &args, const McpServer::Responder &respond) {
			const QJsonObject screen = self->ScreenSummary();
			QString text = QStringLiteral("Location: %1\n").arg(screen.value("screen").toString());
			if(screen.contains("open_layers"))
				text += QStringLiteral("Open: %1\n").arg(screen.value("open_layers").toVariant().toStringList().join(QStringLiteral(" > ")));
			if(screen.contains("selected_tabs"))
				text += QStringLiteral("Tabs: %1\n").arg(screen.value("selected_tabs").toVariant().toStringList().join(QStringLiteral(", ")));
			text += self->ReadScreenText(args.contains("window") ? args.value("window").toInt() : -1);
			respond(McpServer::TextResult(text));
		}),
	});

	server->AddTool({
		"find_ui_elements",
		"Find UI elements",
		"Search the UI for controls by text, type or objectName and return their ids, state and center "
		"coordinates (screenshot space). Best matches (interactive controls, topmost windows) come first.",
		Schema({
			{"text", element_query["text"]},
			{"type", element_query["type"]},
			{"object_name", element_query["object_name"]},
			{"exact", element_query["exact"]},
			{"include_hidden", Prop("boolean", "Also search invisible items")},
			{"limit", Prop("integer", "Maximum results (default 25)")},
		}),
		true,
		guard([self](const QJsonObject &args, const McpServer::Responder &respond) {
			if(args.value("text").toString().isEmpty() && args.value("type").toString().isEmpty() && args.value("object_name").toString().isEmpty())
			{
				respond(McpServer::ErrorResult(QStringLiteral("Provide at least one of text, type or object_name")));
				return;
			}
			const QList<Surface> surfaces = self->Surfaces();
			const Mapping mapping = self->ActiveMapping();
			const QList<ElementRef> found = self->FindElements(surfaces, args);
			const int limit = args.value("limit").toInt(25);
			QJsonArray out;
			for(const ElementRef &ref : found)
			{
				if(out.size() >= limit)
					break;
				if(ref.item)
					out.append(self->DescribeItem(ref.item, surfaces[ref.surface], mapping, ref.id));
			}
			respond(McpServer::TextResult(ToJson(QJsonObject{{"matches", out}, {"total", found.size()}})));
		}),
	});

	auto resolve_element = [self](const QJsonObject &args, QQuickItem **item, int *surface_index, QString *id, QString *error) -> bool {
		const QList<Surface> surfaces = self->Surfaces();
		if(args.contains("id"))
		{
			*id = args.value("id").toString();
			*item = self->ResolveElementId(surfaces, *id, surface_index);
			if(!*item)
			{
				*error = QStringLiteral("No element with id %1 (the UI changed since that id was read). %2")
					.arg(*id, self->SuggestElements(surfaces, QJsonObject()));
				return false;
			}
			return true;
		}
		const QList<ElementRef> found = self->FindElements(surfaces, args);
		const int index = args.value("index").toInt(0);
		if(found.isEmpty() || index < 0 || index >= found.size())
		{
			*error = found.isEmpty()
				? QStringLiteral("No visible element matches. %1").arg(self->SuggestElements(surfaces, args))
				: QStringLiteral("index %1 is out of range: only %2 match(es), use index 0..%3").arg(index).arg(found.size()).arg(found.size() - 1);
			return false;
		}
		*item = found[index].item;
		*surface_index = found[index].surface;
		*id = found[index].id;
		return true;
	};

	server->AddTool({
		"click_element",
		"Click UI element",
		"Click the center of a UI control found by id, text, type or objectName (with a real mouse event, "
		"exactly like a user). Use focus_element + press_key for keyboard/controller style navigation instead.",
		Schema({
			{"id", element_query["id"]}, {"text", element_query["text"]}, {"type", element_query["type"]},
			{"object_name", element_query["object_name"]}, {"exact", element_query["exact"]}, {"index", element_query["index"]},
			{"button", EnumProp({"left", "right", "middle"}, "Mouse button (default left)")},
			{"clicks", Prop("integer", "1 = click (default), 2 = double click")},
		}),
		false,
		guard([self, resolve_element](const QJsonObject &args, const McpServer::Responder &respond) {
			QQuickItem *item = nullptr;
			int surface_index = -1;
			QString id, error;
			if(!resolve_element(args, &item, &surface_index, &id, &error))
			{
				respond(McpServer::ErrorResult(error));
				return;
			}
			const QList<Surface> surfaces = self->Surfaces();
			if(surface_index < 0 || surface_index >= surfaces.size())
			{
				respond(McpServer::ErrorResult(QStringLiteral("The element's window is gone")));
				return;
			}
			const QPointF center_scene = item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
			const QPointF global = QPointF(surfaces[surface_index].global_rect.topLeft()) + center_scene;
			const QJsonObject desc = self->DescribeItem(item, surfaces[surface_index], self->ActiveMapping(), id);
			QString target;
			self->SendMouse(global, ParseButton(args.value("button").toString()), qBound(1, args.value("clicks").toInt(1), 3), Qt::NoModifier, &target);
			respond(McpServer::TextResult(QStringLiteral("Clicked %1 %2 at (%3,%4) in window %5")
					.arg(desc["type"].toString(), desc.contains("text") ? QStringLiteral("\"%1\"").arg(desc["text"].toString()) : id)
					.arg(desc["center_x"].toInt()).arg(desc["center_y"].toInt()).arg(target)));
		}),
	});

	server->AddTool({
		"focus_element",
		"Focus UI element",
		"Give keyboard focus to a UI control (by id, text, type or objectName) without clicking. Afterwards "
		"press_key Return/Space activates it and arrow keys navigate from it.",
		Schema({
			{"id", element_query["id"]}, {"text", element_query["text"]}, {"type", element_query["type"]},
			{"object_name", element_query["object_name"]}, {"exact", element_query["exact"]}, {"index", element_query["index"]},
		}),
		false,
		guard([self, resolve_element](const QJsonObject &args, const McpServer::Responder &respond) {
			QQuickItem *item = nullptr;
			int surface_index = -1;
			QString id, error;
			if(!resolve_element(args, &item, &surface_index, &id, &error))
			{
				respond(McpServer::ErrorResult(error));
				return;
			}
			const QList<Surface> surfaces = self->Surfaces();
			if(surface_index >= 0 && surface_index < surfaces.size() && surfaces[surface_index].window)
				surfaces[surface_index].window->requestActivate();
			item->forceActiveFocus(Qt::OtherFocusReason);
			respond(McpServer::TextResult(QStringLiteral("Focused %1 (%2); active focus: %3")
					.arg(CleanTypeName(item), id, item->hasActiveFocus() ? QStringLiteral("yes") : QStringLiteral("pending window activation"))));
		}),
	});

	const QJsonObject point_props{
		{"x", Prop("number", "X coordinate (see coordinate_space)")},
		{"y", Prop("number", "Y coordinate (see coordinate_space)")},
		{"coordinate_space", CoordinateSpaceProp()},
	};

	server->AddTool({
		"click",
		"Click",
		"Move the mouse to a point and click it, exactly like a user. By default x/y are pixels of the last "
		"screenshot. During a stream (without the menu open) mouse clicks act as the DualSense touchpad.",
		Schema({
			{"x", point_props["x"]}, {"y", point_props["y"]}, {"coordinate_space", point_props["coordinate_space"]},
			{"button", EnumProp({"left", "right", "middle", "back", "forward"}, "Mouse button (default left)")},
			{"clicks", Prop("integer", "1 = click (default), 2 = double click, 3 = triple click")},
			{"hold_ms", Prop("integer", "Keep the button pressed this long (long press)")},
			{"modifiers", ModifiersProp()},
		}, {"x", "y"}),
		false,
		guard([self](const QJsonObject &args, const McpServer::Responder &respond) {
			QPointF global;
			QString error;
			if(!self->PointFromArgs(args, "x", "y", &global, &error))
			{
				respond(McpServer::ErrorResult(error));
				return;
			}
			const Qt::MouseButton button = ParseButton(args.value("button").toString());
			const Qt::KeyboardModifiers mods = ParseModifiers(args.value("modifiers"));
			const int hold_ms = qBound(0, args.value("hold_ms").toInt(0), kMaxWaitMs);
			if(hold_ms > 0)
			{
				self->SendMouseMove(global, Qt::NoButton, mods);
				self->SendMouseButton(global, button, true, button, mods);
				QTimer::singleShot(hold_ms, self.data(), [self, global, button, mods, respond]() {
					if(!self)
						return;
					self->SendMouseButton(global, button, false, Qt::NoButton, mods);
					respond(McpServer::TextResult(QStringLiteral("Long pressed")));
				});
				return;
			}
			QString target;
			self->SendMouse(global, button, qBound(1, args.value("clicks").toInt(1), 3), mods, &target);
			respond(McpServer::TextResult(QStringLiteral("Clicked in window %1").arg(target)));
		}),
	});

	server->AddTool({
		"mouse_move",
		"Move mouse",
		"Move the mouse pointer (hover) to a point without clicking.",
		Schema(point_props, {"x", "y"}),
		false,
		guard([self](const QJsonObject &args, const McpServer::Responder &respond) {
			QPointF global;
			QString error;
			if(!self->PointFromArgs(args, "x", "y", &global, &error))
			{
				respond(McpServer::ErrorResult(error));
				return;
			}
			self->SendMouseMove(global, Qt::NoButton, Qt::NoModifier);
			respond(McpServer::TextResult(QStringLiteral("Moved")));
		}),
	});

	server->AddTool({
		"drag",
		"Drag",
		"Press at a start point, move smoothly to an end point and release (sliders, scrolling lists, swipes).",
		Schema({
			{"from_x", Prop("number", "Start X")}, {"from_y", Prop("number", "Start Y")},
			{"to_x", Prop("number", "End X")}, {"to_y", Prop("number", "End Y")},
			{"coordinate_space", CoordinateSpaceProp()},
			{"duration_ms", Prop("integer", "How long the movement takes (default 300)")},
			{"button", EnumProp({"left", "right", "middle"}, "Mouse button (default left)")},
		}, {"from_x", "from_y", "to_x", "to_y"}),
		false,
		guard([self](const QJsonObject &args, const McpServer::Responder &respond) {
			QPointF from, to;
			QString error;
			if(!self->PointFromArgs(args, "from_x", "from_y", &from, &error) || !self->PointFromArgs(args, "to_x", "to_y", &to, &error))
			{
				respond(McpServer::ErrorResult(error));
				return;
			}
			const Qt::MouseButton button = ParseButton(args.value("button").toString());
			const int duration = qBound(16, args.value("duration_ms").toInt(300), kMaxWaitMs);
			const int steps = qMax(2, duration / 16);
			self->SendMouseMove(from, Qt::NoButton, Qt::NoModifier);
			self->SendMouseButton(from, button, true, button, Qt::NoModifier);
			auto step = QSharedPointer<int>::create(0);
			auto *timer = new QTimer(self.data());
			timer->setInterval(duration / steps);
			QObject::connect(timer, &QTimer::timeout, self.data(), [self, timer, step, steps, from, to, button, respond]() {
				(*step)++;
				const double t = double(*step) / steps;
				const QPointF p = from + (to - from) * t;
				self->SendMouseMove(p, button, Qt::NoModifier);
				if(*step >= steps)
				{
					timer->stop();
					timer->deleteLater();
					self->SendMouseButton(to, button, false, Qt::NoButton, Qt::NoModifier);
					respond(McpServer::TextResult(QStringLiteral("Dragged")));
				}
			});
			timer->start();
		}),
	});

	server->AddTool({
		"scroll",
		"Scroll",
		"Scroll the mouse wheel at a point. delta_y > 0 scrolls down, delta_x > 0 scrolls right; units are wheel notches.",
		Schema({
			{"x", point_props["x"]}, {"y", point_props["y"]}, {"coordinate_space", point_props["coordinate_space"]},
			{"delta_x", Prop("number", "Horizontal notches (default 0)")},
			{"delta_y", Prop("number", "Vertical notches (default 0)")},
		}, {"x", "y"}),
		false,
		guard([self](const QJsonObject &args, const McpServer::Responder &respond) {
			QPointF global;
			QString error;
			if(!self->PointFromArgs(args, "x", "y", &global, &error))
			{
				respond(McpServer::ErrorResult(error));
				return;
			}
			const QList<Surface> surfaces = self->Surfaces();
			const int index = self->SurfaceAt(surfaces, global);
			if(index < 0)
			{
				respond(McpServer::ErrorResult(QStringLiteral("No window at that point")));
				return;
			}
			self->SendMouseMove(global, Qt::NoButton, Qt::NoModifier);
			QWindow *window = surfaces[index].window;
			// Wheel convention: positive angle delta scrolls up/left.
			const QPoint angle(qRound(-args.value("delta_x").toDouble() * 120), qRound(-args.value("delta_y").toDouble() * 120));
			QWindowSystemInterface::handleWheelEvent(window, window->mapFromGlobal(global), global, QPoint(), angle);
			QWindowSystemInterface::flushWindowSystemEvents();
			respond(McpServer::TextResult(QStringLiteral("Scrolled")));
		}),
	});

	server->AddTool({
		"press_key",
		"Press key",
		"Press and release a key or key combination in the focused app window, e.g. \"Return\", \"Escape\", "
		"\"Tab\", \"Up\", \"ctrl+o\" (stream menu), \"F11\" (fullscreen). The app's UI is fully keyboard "
		"navigable: arrows move focus, Return/Space activate, Escape goes back. During a stream (menu closed) "
		"keys are mapped to controller buttons; prefer controller_input there.",
		Schema({
			{"key", Prop("string", "Key or combination, modifiers joined with '+'")},
			{"repeat", Prop("integer", "Press it this many times (default 1)")},
			{"activate_window", Prop("boolean", "Bring the app to the foreground first if it lacks keyboard focus (default true)")},
		}, {"key"}),
		false,
		guard([self](const QJsonObject &args, const McpServer::Responder &respond) {
			self->EnsureKeyboardTarget(args.value("activate_window").toBool(true));
			QString error;
			const int repeat = qBound(1, args.value("repeat").toInt(1), 100);
			if(!self->SendKeyCombination(args.value("key").toString(), repeat, &error))
			{
				respond(McpServer::ErrorResult(error));
				return;
			}
			respond(McpServer::TextResult(QStringLiteral("Pressed %1%2").arg(args.value("key").toString(),
					repeat > 1 ? QStringLiteral(" x%1").arg(repeat) : QString())));
		}),
	});

	server->AddTool({
		"type_text",
		"Type text",
		"Type text into the focused text field (click or focus it first). \\n presses Return.",
		Schema({
			{"text", Prop("string", "Text to type")},
			{"activate_window", Prop("boolean", "Bring the app to the foreground first if it lacks keyboard focus (default true)")},
		}, {"text"}),
		false,
		guard([self](const QJsonObject &args, const McpServer::Responder &respond) {
			self->EnsureKeyboardTarget(args.value("activate_window").toBool(true));
			const QString text = args.value("text").toString();
			self->SendText(text);
			respond(McpServer::TextResult(QStringLiteral("Typed %1 characters").arg(text.size())));
		}),
	});

	server->AddTool({
		"window_control",
		"Control window",
		"Change the main window: activate (bring to front), restore, minimize, maximize, fullscreen, "
		"exit_fullscreen, resize (width/height in logical pixels) or move (x/y).",
		Schema({
			{"action", EnumProp({"activate", "restore", "minimize", "maximize", "fullscreen", "exit_fullscreen", "resize", "move"}, "What to do")},
			{"width", Prop("integer", "For resize")}, {"height", Prop("integer", "For resize")},
			{"x", Prop("integer", "For move")}, {"y", Prop("integer", "For move")},
		}, {"action"}),
		false,
		guard([self](const QJsonObject &args, const McpServer::Responder &respond) {
			QWindow *w = self->main_window;
			const QString action = args.value("action").toString();
			if(action == QLatin1String("activate"))
			{
				if(w->visibility() == QWindow::Minimized)
					w->showNormal();
				w->raise();
				w->requestActivate();
			}
			else if(action == QLatin1String("restore"))
				w->showNormal();
			else if(action == QLatin1String("minimize"))
				w->showMinimized();
			else if(action == QLatin1String("maximize"))
				w->showMaximized();
			else if(action == QLatin1String("fullscreen"))
				w->showFullScreen();
			else if(action == QLatin1String("exit_fullscreen"))
				w->showNormal();
			else if(action == QLatin1String("resize"))
			{
				const int width = args.value("width").toInt(), height = args.value("height").toInt();
				if(width <= 0 || height <= 0)
				{
					respond(McpServer::ErrorResult(QStringLiteral("resize needs positive width and height")));
					return;
				}
				if(w->visibility() == QWindow::FullScreen || w->visibility() == QWindow::Maximized)
					w->showNormal();
				w->resize(width, height);
			}
			else if(action == QLatin1String("move"))
				w->setPosition(args.value("x").toInt(), args.value("y").toInt());
			else
			{
				respond(McpServer::ErrorResult(QStringLiteral("Unknown action %1").arg(action)));
				return;
			}
			// Window manager changes are asynchronous; report the state shortly after.
			QTimer::singleShot(300, self.data(), [self, respond]() {
				if(self)
					respond(McpServer::TextResult(ToJson(self->WindowInfo())));
			});
		}),
	});

	server->AddTool({
		"wait_for",
		"Wait for condition",
		"Wait until something happens instead of guessing sleep times: an element appears (until=element) or "
		"disappears (element_gone), the picture changes from how it looks now (screen_changed, e.g. after an input "
		"to the console) or stops changing (screen_stable, e.g. a loading animation finished). Returns as soon as the "
		"condition holds, or reports timed_out.",
		Schema({
			{"until", EnumProp({"element", "element_gone", "screen_changed", "screen_stable"}, "Condition to wait for")},
			{"text", Prop("string", "For element conditions: visible text to look for")},
			{"type", Prop("string", "For element conditions: control type")},
			{"object_name", Prop("string", "For element conditions: objectName")},
			{"exact", Prop("boolean", "For element conditions: whole text must match")},
			{"timeout_ms", Prop("integer", "Give up after this long (default 10000, max 60000)")},
			{"interval_ms", Prop("integer", "Polling interval (default 300)")},
			{"threshold", Prop("number", "Screen conditions: difference 0-255 of the most changed screen region that counts as a change (default 2)")},
			{"screenshot", Prop("boolean", "Take a screenshot when done (default true)")},
			{"annotate", Prop("boolean", "Annotate that screenshot with numbered element marks")},
		}, {"until"}),
		true,
		guard([self](const QJsonObject &args, const McpServer::Responder &respond) {
			self->WaitFor(args, QImage(), respond);
		}),
	});

	server->AddTool({
		"wait",
		"Wait",
		"Wait for animations, loading or the console to react, optionally followed by a screenshot.",
		Schema({
			{"ms", Prop("integer", "Milliseconds to wait (max 30000)")},
			{"screenshot", Prop("boolean", "Take a screenshot afterwards (default false)")},
			{"annotate", Prop("boolean", "Annotate that screenshot with numbered element marks")},
		}, {"ms"}),
		true,
		guard([self](const QJsonObject &args, const McpServer::Responder &respond) {
			const int ms = qBound(0, args.value("ms").toInt(), kMaxWaitMs);
			const bool shot = args.value("screenshot").toBool(false);
			const bool annotate = args.value("annotate").toBool(false);
			QTimer::singleShot(ms, self.data(), [self, ms, shot, annotate, respond]() {
				if(!self)
					return;
				if(shot)
					self->Screenshot(QJsonObject{{"annotate", annotate}}, respond);
				else
					respond(McpServer::TextResult(QStringLiteral("Waited %1 ms").arg(ms)));
			});
		}),
	});
}
