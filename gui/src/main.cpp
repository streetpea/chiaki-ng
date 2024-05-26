
// ugly workaround because Windows does weird things and ENOTIME
int real_main(int argc, char *argv[]);
int main(int argc, char *argv[]) { return real_main(argc, argv); }

#include <streamsession.h>
#include <settings.h>
#include <host.h>
#include <controllermanager.h>
#include <discoverymanager.h>
#include <qmlmainwindow.h>
#include <QGuiApplication>

#ifdef CHIAKI_ENABLE_CLI
#include <chiaki-cli.h>
#endif

#include <chiaki/session.h>
#include <chiaki/regist.h>
#include <chiaki/base64.h>

#include <stdio.h>
#include <string.h>

#include <QCommandLineParser>
#include <QMap>
#include <QSurfaceFormat>

Q_DECLARE_METATYPE(ChiakiLogLevel)
Q_DECLARE_METATYPE(ChiakiRegistEventType)

#if defined(CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE) && defined(Q_OS_LINUX)
#include <QtPlugin>
Q_IMPORT_PLUGIN(SDInputContextPlugin)
#endif

#ifdef CHIAKI_ENABLE_CLI
struct CLICommand
{
	int (*cmd)(ChiakiLog *log, int argc, char *argv[]);
};

static const QMap<QString, CLICommand> cli_commands = {
	{ "discover", { chiaki_cli_cmd_discover } },
	{ "wakeup", { chiaki_cli_cmd_wakeup } }
};
#endif

int RunStream(QGuiApplication &app, const StreamSessionConnectInfo &connect_info);
int RunMain(QGuiApplication &app, Settings *settings);

int real_main(int argc, char *argv[])
{
	qRegisterMetaType<DiscoveryHost>();
	qRegisterMetaType<RegisteredHost>();
	qRegisterMetaType<HostMAC>();
	qRegisterMetaType<ChiakiQuitReason>();
	qRegisterMetaType<ChiakiRegistEventType>();
	qRegisterMetaType<ChiakiLogLevel>();

	QGuiApplication::setOrganizationName("Chiaki");
	QGuiApplication::setApplicationName("Chiaki");
	QGuiApplication::setApplicationVersion(CHIAKI_VERSION);
	QGuiApplication::setApplicationDisplayName("chiaki4deck");
	QGuiApplication::setDesktopFileName("chiaki4deck");

	qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu");
#if defined(Q_OS_WIN)
	QString import_path = QFileInfo(argv[0]).dir().absolutePath() + "/qml";
	qputenv("QML_IMPORT_PATH", import_path.toUtf8());
#endif
#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
	if (qEnvironmentVariableIsSet("SteamDeck") || qEnvironmentVariable("DESKTOP_SESSION").contains("steamos"))
		qputenv("QT_IM_MODULE", "sdinput");
#endif

	ChiakiErrorCode err = chiaki_lib_init();
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(stderr, "Chiaki lib init failed: %s\n", chiaki_error_string(err));
		return 1;
	}

    SDL_SetHint(SDL_HINT_APP_NAME, "chiaki4deck");

	if(SDL_Init(SDL_INIT_AUDIO) < 0)
	{
		fprintf(stderr, "SDL Audio init failed: %s\n", SDL_GetError());
		return 1;
	}

	QGuiApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

	QGuiApplication app(argc, argv);

#ifdef Q_OS_MACOS
	QGuiApplication::setWindowIcon(QIcon(":/icons/chiaki_macos.svg"));
#else
	QGuiApplication::setWindowIcon(QIcon(":/icons/chiaki4deck.svg"));
#endif

	QCommandLineParser parser;
	parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsPositionalArguments);
	parser.addHelpOption();
	
	QStringList cmds;
	cmds.append("stream");
	cmds.append("list");
#ifdef CHIAKI_ENABLE_CLI
	cmds.append(cli_commands.keys());
#endif

	parser.addPositionalArgument("command", cmds.join(", "));
	parser.addPositionalArgument("nickname", "Needed for stream command to get credentials for connecting. "
			"Use 'list' to get the nickname.");
	parser.addPositionalArgument("host", "Address to connect to (when using the stream command).");

	QCommandLineOption profile_option("profile", "", "profile", "Configuration profile");
	parser.addOption(profile_option);

	QCommandLineOption regist_key_option("registkey", "", "registkey");
	parser.addOption(regist_key_option);

	QCommandLineOption morning_option("morning", "", "morning");
	parser.addOption(morning_option);

	QCommandLineOption fullscreen_option("fullscreen", "Start window in fullscreen mode [maintains aspect ratio, adds black bars to fill unsused parts of screen if applicable] (only for use with stream command).");
	parser.addOption(fullscreen_option);

	QCommandLineOption dualsense_option("dualsense", "Enable DualSense haptics and adaptive triggers (PS5 and DualSense connected via USB only).");
	parser.addOption(dualsense_option);

	QCommandLineOption zoom_option("zoom", "Start window in fullscreen zoomed in to fit screen [maintains aspect ratio, cutting off edges of image to fill screen] (only for use with stream command)");
	parser.addOption(zoom_option);

	QCommandLineOption stretch_option("stretch", "Start window in fullscreen stretched to fit screen [distorts aspect ratio to fill screen] (only for use with stream command).");
	parser.addOption(stretch_option);

	QCommandLineOption passcode_option("passcode", "Automatically send your PlayStation login passcode (only affects users with a login passcode set on their PlayStation console).", "passcode");
	parser.addOption(passcode_option);

	parser.process(app);
	QStringList args = parser.positionalArguments();

	Settings settings(parser.isSet(profile_option) ? parser.value(profile_option) : QString());

	if(args.length() == 0)
		return RunMain(app, &settings);

	if(args[0] == "list")
	{
		for(const auto &host : settings.GetRegisteredHosts())
			printf("Host: %s \n", host.GetServerNickname().toLocal8Bit().constData());
		return 0;
	}
	if(args[0] == "stream")
	{
		if(args.length() < 2)
			parser.showHelp(1);

		//QString host = args[sizeof(args) -1]; //the ip is always the last param for stream
		QString host = args[args.size()-1];
		QByteArray morning;
		QByteArray regist_key;
		QString initial_login_passcode;
		ChiakiTarget target = CHIAKI_TARGET_PS4_10;

		if(parser.value(regist_key_option).isEmpty() && parser.value(morning_option).isEmpty())
		{
			if(args.length() < 3)
				parser.showHelp(1);

			bool found = false;
			for(const auto &temphost : settings.GetRegisteredHosts())
			{
				if(temphost.GetServerNickname() == args[1])
				{
					found = true;
					morning = temphost.GetRPKey();
					regist_key = temphost.GetRPRegistKey();
					target = temphost.GetTarget();
					break;
				}
			}
			if(!found)
			{
				printf("No configuration found for '%s'\n", args[1].toLocal8Bit().constData());
				return 1;
			}
		}
		else
		{
			// TODO: explicit option for target
			regist_key = parser.value(regist_key_option).toUtf8();
			if(regist_key.length() > sizeof(ChiakiConnectInfo::regist_key))
			{
				printf("Given regist key is too long (expected size <=%llu, got %d)\n",
					(unsigned long long)sizeof(ChiakiConnectInfo::regist_key),
					regist_key.length());
				return 1;
			}
			regist_key += QByteArray(sizeof(ChiakiConnectInfo::regist_key) - regist_key.length(), 0);
			morning = QByteArray::fromBase64(parser.value(morning_option).toUtf8());
			if(morning.length() != sizeof(ChiakiConnectInfo::morning))
			{
				printf("Given morning has invalid size (expected %llu, got %d)\n",
					(unsigned long long)sizeof(ChiakiConnectInfo::morning),
					morning.length());
				printf("Given morning has invalid size (expected %llu)", (unsigned long long)sizeof(ChiakiConnectInfo::morning));
				return 1;
			}
		}
		if ((parser.isSet(stretch_option) && (parser.isSet(zoom_option) || parser.isSet(fullscreen_option))) || (parser.isSet(zoom_option) && parser.isSet(fullscreen_option)))
		{
			printf("Must choose between fullscreen, zoom or stretch option.");
			return 1;
		}
		if(parser.value(passcode_option).isEmpty())
		{
			//Set to empty if it wasn't given by user.
			initial_login_passcode = QString("");
		}
		else
		{
			initial_login_passcode = parser.value(passcode_option);
			if(initial_login_passcode.length() != 4)
			{
				printf("Login passcode must be 4 digits. You entered %d digits)\n", initial_login_passcode.length());
				return 1;
			}
		}
		
		StreamSessionConnectInfo connect_info(
				&settings,
				target,
				host,
				regist_key,
				morning,
				initial_login_passcode,
				QString(),
				parser.isSet(fullscreen_option),
				parser.isSet(zoom_option),
				parser.isSet(stretch_option));

		return RunStream(app, connect_info);
	}
#ifdef CHIAKI_ENABLE_CLI
	else if(cli_commands.contains(args[0]))
	{
		ChiakiLog log;
		// TODO: add verbose arg
		chiaki_log_init(&log, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE, chiaki_log_cb_print, nullptr);

		const auto &cmd = cli_commands[args[0]];
		int sub_argc = args.count();
		QVector<QByteArray> sub_argv_b(sub_argc);
		QVector<char *> sub_argv(sub_argc);
		for(size_t i=0; i<sub_argc; i++)
		{
			sub_argv_b[i] = args[i].toLocal8Bit();
			sub_argv[i] = sub_argv_b[i].data();
		}
		return cmd.cmd(&log, sub_argc, sub_argv.data());
	}
#endif
	else
	{
		parser.showHelp(1);
	}
}

int RunMain(QGuiApplication &app, Settings *settings)
{
	QmlMainWindow main_window(settings);
	main_window.show();
	return app.exec();
}

int RunStream(QGuiApplication &app, const StreamSessionConnectInfo &connect_info)
{
	QmlMainWindow main_window(connect_info);
	main_window.show();
	return app.exec();
}
