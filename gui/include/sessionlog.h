// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SESSIONLOG_H
#define CHIAKI_SESSIONLOG_H

#include <chiaki/log.h>

#include <QString>
#include <QDir>
#include <QMutex>
#include <QAtomicInteger>

class QFile;
class StreamSession;

class SessionLog
{
	friend class SessionLogPrivate;

	private:
		StreamSession *session;
		ChiakiLog log;
		QFile *file;
		QMutex file_mutex;
		QAtomicInteger<bool> shutdown;
		bool sanitize;

		void Log(ChiakiLogLevel level, const char *msg);

	public:
		SessionLog(StreamSession *session, uint32_t level_mask, const QString &filename, bool sanitize);
		~SessionLog();

		ChiakiLog *GetChiakiLog()	{ return &log; }
		void PrepareShutdown();
};

QString GetLogBaseDir();
QString CreateLogFilename();

#endif //CHIAKI_SESSIONLOG_H
