/*
 * SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
 *
 * RUDP Implementation
 * --------------------------------
 *
 * "Remote Play over Internet" uses a custom UDP-based protocol named "SCE RUDP" for communication between the
 * console and the client for the portions that use TCP for a local connection.
 *
 * .
 */

#ifndef CHIAKI_RUDP_H
#define CHIAKI_RUDP_H

#include <stdint.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <chiaki/log.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Handle to rudp session state */
typedef struct rudp_t* Rudp;


/**
 * Create rudp instance
 *
 * @param[in] rudp The Rudp instance to be initialized
 * @param[in] log The ChiakiLog to use for log messages
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
*/
CHIAKI_EXPORT ChiakiErrorCode init_rudp(
    Rudp rudp, ChiakiLog *log);


/**
 * Terminate rudp instance
 *
 * @param[in] rudp The Rudp instance to be destroyed
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
*/
CHIAKI_EXPORT void chiaki_rudp_fini(Rudp rudp);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_RUDP_H