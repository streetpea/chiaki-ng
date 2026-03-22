// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <sessionlog.h>
#include <chiaki/log.h>

#include <QStandardPaths>
#include <QDir>
#include <QRegularExpression>
#include <QDateTime>
#include <QFile>
#include <QPair>
#include <QVector>
#include <QThread>


static void LogCb(ChiakiLogLevel level, const char *msg, void *user);

SessionLog::SessionLog(StreamSession *session, uint32_t level_mask, const QString &filename)
	: session(session), shutdown(false)
{
	chiaki_log_init(&log, level_mask, LogCb, this);

	if(filename.isEmpty())
	{
		file = nullptr;
		CHIAKI_LOGI(&log, "Logging to file disabled");
	}
	else
	{
		file = new QFile(filename);
		if(!file->open(QIODevice::ReadWrite))
		{
			delete file;
			file = nullptr;
			CHIAKI_LOGI(&log, "Failed to open file %s for logging", filename.toLocal8Bit().constData());
		}
		else
		{
			CHIAKI_LOGI(&log, "Logging to file %s", filename.toLocal8Bit().constData());
		}
	}

	CHIAKI_LOGI(&log, "Chiaki Version " CHIAKI_VERSION);
}

SessionLog::~SessionLog()
{
	PrepareShutdown();
}

void SessionLog::PrepareShutdown()
{
	// Signal that we're shutting down - no more logging allowed
	shutdown.storeRelaxed(true);

	// Wait a moment for any in-flight log calls to complete
	QThread::msleep(10);

	// Now safe to clean up
	QMutexLocker lock(&file_mutex);
	if(file)
	{
		file->flush();
		delete file;
		file = nullptr;
	}
}

void SessionLog::Log(ChiakiLogLevel level, const char *msg)
{
	// Check if we're shutting down - if so, skip file logging
	if(shutdown.loadRelaxed())
	{
		chiaki_log_cb_print(level, msg, nullptr);
		return;
	}

	chiaki_log_cb_print(level, msg, nullptr);

	QMutexLocker lock(&file_mutex);
	if(file && !shutdown.loadRelaxed())
	{
		static const QString date_format = "yyyy-MM-dd HH:mm:ss:zzzzzz";
		QString str = QString("[%1] [%2] %3\n").arg(
				QDateTime::currentDateTime().toString(date_format),
				QString(chiaki_log_level_char(level)),
				msg);

		file->write(str.toLocal8Bit());
		file->flush();
	}
}

class SessionLogPrivate
{
	public:
		static void Log(SessionLog *log, ChiakiLogLevel level, const char *msg) { log->Log(level, msg); }
};

static void LogCb(ChiakiLogLevel level, const char *msg, void *user)
{
	auto log = reinterpret_cast<SessionLog *>(user);
	SessionLogPrivate::Log(log, level, msg);
}

#define KEEP_LOG_FILES_COUNT 5

QString GetLogBaseDir()
{
	auto base_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if(base_dir.isEmpty())
		return QString();

	QDir dir(base_dir);
	if(!dir.mkpath("log"))
		return QString();
	if(!dir.cd("log"))
		return QString();

	return dir.absolutePath();
}

QString CreateLogFilename()
{
	static const QString date_format = "yyyy-MM-dd_HH-mm-ss-zzzzzz";
	static const QString session_log_wildcard = "chiaki_session_*.log";
	static const QRegularExpression session_log_regex("chiaki_session_(.*).log");

	QString dir_str = GetLogBaseDir();
	if(dir_str.isEmpty())
		return QString();
	QDir dir = QDir(dir_str);

	dir.setNameFilters({ session_log_wildcard });
	auto existing_files = dir.entryList();
	QVector<QPair<QString, QDateTime>> existing_files_date;
	existing_files_date.resize(existing_files.count());
	std::transform(existing_files.begin(), existing_files.end(), existing_files_date.begin(), [](const QString &filename) {
		QDateTime date;
		auto match = session_log_regex.match(filename);
		if(match.hasMatch())
			date = QDateTime::fromString(match.captured(1), date_format);
		return QPair<QString, QDateTime>(filename, date);
	});
	std::sort(existing_files_date.begin(), existing_files_date.end(), [](const QPair<QString, QDateTime> &a, const QPair<QString, QDateTime> &b) {
		return a.second > b.second;
	});

	for(int i=KEEP_LOG_FILES_COUNT; i<existing_files_date.count(); i++)
	{
		const auto &pair = existing_files_date[i];
		if(!pair.second.isValid())
			break;
		dir.remove(pair.first);
	}

	QString filename = "chiaki_session_" + QDateTime::currentDateTime().toString(date_format) + ".log";
	return dir.absoluteFilePath(filename);
}
