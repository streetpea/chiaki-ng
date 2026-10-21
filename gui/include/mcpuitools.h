// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_MCPUITOOLS_H
#define CHIAKI_MCPUITOOLS_H

#include "mcpserver.h"

#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QRect>
#include <QSet>

#include <functional>

class QQuickItem;
class QQuickWindow;
class QWidget;
class QWindow;

/**
 * Generic MCP tools that let an AI agent see and drive a Qt Quick application
 * the same way a person does: full-window screenshots (every visible app
 * window composited, nothing cropped), an accessibility-style UI tree and
 * real mouse/keyboard input injected through the platform event queue.
 *
 * All coordinates exchanged with the agent are, by default, pixels of the
 * most recent screenshot, so "click where you see it" just works.
 */
class McpUiTools : public QObject
{
	Q_OBJECT

	public:
		using FrameCallback = std::function<void(const QImage &frame, const QString &error)>;
		// Delivers the main window's client area exactly as presented, in device pixels.
		using FrameGrabber = std::function<void(const FrameCallback &callback)>;

		McpUiTools(QWindow *main_window, QQuickWindow *main_scene, QObject *parent = nullptr);

		void SetMainFrameGrabber(const FrameGrabber &grabber) { main_grabber = grabber; }
		// Windows that are never shown on screen (e.g. render control offscreen windows).
		void AddExcludedWindow(QWindow *window) { excluded_windows.insert(window); }

		void RegisterTools(McpServer *server);
		// Adds screenshot_after / screenshot_delay_ms / annotate_screenshot / expect to the named tools.
		void EnableScreenshotAfter(McpServer *server, const QStringList &tool_names);

		// Shared with the app specific tool set.
		void TakeScreenshot(const QJsonObject &args, const McpServer::Responder &respond);
		QJsonObject WindowInfo() const;
		// Which view/dialog/tab is showing and what has focus: "where am I" without a screenshot.
		QJsonObject ScreenSummary() const;
		// Runs generic health checks (window, capture, UI access, focus); results arrive asynchronously.
		void SelfCheck(const std::function<void(const QJsonArray &checks)> &done);

	private:
		struct Surface
		{
			QPointer<QWindow> window;
			QPointer<QQuickWindow> scene;
			QPointer<QWidget> widget;
			QRect global_rect; // logical pixels, client area
			QString kind;
			bool is_main = false;
			bool accepts_input = true;
		};

		struct Mapping
		{
			bool valid = false;
			QPointF origin_offset; // canvas origin relative to the main window position, logical pixels
			double scale = 1.0;    // image pixels per logical pixel
			QSize image_size;
		};

		struct ElementRef
		{
			QPointer<QQuickItem> item;
			int surface = -1;
			QString id;
		};

		QList<Surface> Surfaces() const;
		Mapping CurrentMapping(const QList<Surface> &surfaces) const;
		Mapping ActiveMapping() const;
		double MainDpr() const;

		bool PointFromArgs(const QJsonObject &args, const QString &x_key, const QString &y_key, QPointF *global, QString *error) const;
		QPointF ImageToGlobal(const QPointF &image_point, const Mapping &mapping) const;
		QRectF GlobalToImage(const QRectF &global_rect, const Mapping &mapping) const;
		int SurfaceAt(const QList<Surface> &surfaces, const QPointF &global) const;
		QWindow *KeyboardTarget(const QList<Surface> &surfaces) const;

		void SendMouse(const QPointF &global, Qt::MouseButton button, int clicks, Qt::KeyboardModifiers modifiers, QString *target_name);
		void SendMouseMove(const QPointF &global, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers);
		void SendMouseButton(const QPointF &global, Qt::MouseButton button, bool press, Qt::MouseButtons buttons_after, Qt::KeyboardModifiers modifiers);
		bool SendKeyCombination(const QString &combo, int repeat, QString *error);
		void SendText(const QString &text);
		void EnsureKeyboardTarget(bool activate);

		QQuickItem *RootItem(const Surface &surface) const;
		QQuickItem *ResolveElementId(const QList<Surface> &surfaces, const QString &id, int *surface_index) const;
		QList<ElementRef> FindElements(const QList<Surface> &surfaces, const QJsonObject &query) const;
		// "Did you mean" list of visible controls resembling a failed query.
		QString SuggestElements(const QList<Surface> &surfaces, const QJsonObject &query) const;
		QString ReadScreenText(int only_window) const;
		QJsonObject DescribeItem(QQuickItem *item, const Surface &surface, const Mapping &mapping, const QString &id) const;
		void DumpTree(QQuickItem *item, const Surface &surface, int surface_index, const QString &path, int depth,
				const Mapping &mapping, const QJsonObject &options, QStringList *lines, QJsonArray *json, int *count) const;

		void Screenshot(const QJsonObject &args, const McpServer::Responder &respond);
		// wait_for implementation; baseline_frame is the screen before an action for screen_changed.
		void WaitFor(const QJsonObject &args, const QImage &baseline_frame, const McpServer::Responder &respond);
		void RunWithExpectation(const McpServer::Handler &action, const QJsonObject &args, const McpServer::Responder &respond);
		QJsonArray AnnotateImage(QImage *image, const QList<Surface> &surfaces, const Mapping &mapping) const;
		void ComposeAndReply(const QList<Surface> &surfaces, const QImage &main_frame, const QJsonObject &args, const McpServer::Responder &respond);
		// Paints every visible surface onto one canvas (global logical rect *canvas, *scale image px per logical px).
		QImage ComposeCanvas(const QList<Surface> &surfaces, const QImage &main_frame, QRect *canvas, double *scale, QJsonArray *layers) const;
		// Front-to-back is the reverse of this; indices into surfaces.
		QList<int> PaintOrder(const QList<Surface> &surfaces) const;
		// Main frame plus all other app windows, composited (what take_screenshot shows, unencoded).
		void GrabComposite(const std::function<void(const QImage &image, const QString &error)> &callback);

		QPointer<QWindow> main_window;
		QPointer<QQuickWindow> main_scene;
		FrameGrabber main_grabber;
		QSet<QWindow *> excluded_windows;
		Mapping last_mapping;
		QPointF last_mouse_global;
		bool has_last_mouse = false;
};

#endif // CHIAKI_MCPUITOOLS_H
