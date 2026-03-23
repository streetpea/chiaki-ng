// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_THREAD_H
#define CHIAKI_THREAD_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef void *(*ChiakiThreadFunc)(void *);

typedef enum {
	CHIAKI_THREAD_NAME_CTRL,
	CHIAKI_THREAD_NAME_CONGESTION,
	CHIAKI_THREAD_NAME_DISCOVERY,
	CHIAKI_THREAD_NAME_DISCOVERY_SVC,
	CHIAKI_THREAD_NAME_TAKION,
	CHIAKI_THREAD_NAME_TAKION_SEND,
	CHIAKI_THREAD_NAME_RUDP_SEND,
	CHIAKI_THREAD_NAME_HOLEPUNCH,
	CHIAKI_THREAD_NAME_FEEDBACK,
	CHIAKI_THREAD_NAME_SESSION,
	CHIAKI_THREAD_NAME_REGIST,
	CHIAKI_THREAD_NAME_GKCRYPT
} ChiakiThreadName;

typedef void (*ChiakiThreadAffinityFunc)(ChiakiThreadName name, void *user);

typedef struct chiaki_thread_t
{
#ifdef _WIN32
	HANDLE thread;
	ChiakiThreadFunc func;
	void *arg;
	void *ret;
#else
	pthread_t thread;
#endif
} ChiakiThread;

CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_create(ChiakiThread *thread, ChiakiThreadFunc func, void *arg);
CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_join(ChiakiThread *thread, void **retval);
CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_timedjoin(ChiakiThread *thread, void **retval, uint64_t timeout_ms);
CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_set_name(ChiakiThread *thread, const char *name);
CHIAKI_EXPORT void chiaki_thread_set_affinity(ChiakiThreadName name);
CHIAKI_EXPORT void chiaki_thread_set_affinity_cb(ChiakiThreadAffinityFunc func, void *user);


typedef struct chiaki_mutex_t
{
#ifdef _WIN32
	CRITICAL_SECTION cs;
#else
	pthread_mutex_t mutex;
#endif
} ChiakiMutex;

CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_init(ChiakiMutex *mutex, bool rec);
CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_fini(ChiakiMutex *mutex);
CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_lock(ChiakiMutex *mutex);
CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_trylock(ChiakiMutex *mutex);
CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_unlock(ChiakiMutex *mutex);


typedef struct chiaki_cond_t
{
#ifdef _WIN32
	CONDITION_VARIABLE cond;
#else
	pthread_cond_t cond;
#endif
} ChiakiCond;

typedef bool (*ChiakiCheckPred)(void *);

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_init(ChiakiCond *cond);
CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_fini(ChiakiCond *cond);
CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_wait(ChiakiCond *cond, ChiakiMutex *mutex);
CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_timedwait(ChiakiCond *cond, ChiakiMutex *mutex, uint64_t timeout_ms);
CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_wait_pred(ChiakiCond *cond, ChiakiMutex *mutex, ChiakiCheckPred check_pred, void *check_pred_user);
CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_timedwait_pred(ChiakiCond *cond, ChiakiMutex *mutex, uint64_t timeout_ms, ChiakiCheckPred check_pred, void *check_pred_user);
CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_signal(ChiakiCond *cond);
CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_broadcast(ChiakiCond *cond);


typedef struct chiaki_bool_pred_cond_t
{
	ChiakiCond cond;
	ChiakiMutex mutex;
	bool pred;
} ChiakiBoolPredCond;

CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_init(ChiakiBoolPredCond *cond);
CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_fini(ChiakiBoolPredCond *cond);
CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_lock(ChiakiBoolPredCond *cond);
CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_unlock(ChiakiBoolPredCond *cond);
CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_wait(ChiakiBoolPredCond *cond);
CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_timedwait(ChiakiBoolPredCond *cond, uint64_t timeout_ms);
CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_signal(ChiakiBoolPredCond *cond);
CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_broadcast(ChiakiBoolPredCond *cond);

#ifdef __cplusplus
}

class ChiakiMutexLock
{
	private:
		ChiakiMutex * const mutex;

	public:
		ChiakiMutexLock(ChiakiMutex *mutex) : mutex(mutex) { chiaki_mutex_lock(mutex); }
		~ChiakiMutexLock() { chiaki_mutex_unlock(mutex); }
};
#endif

#endif // CHIAKI_THREAD_H
