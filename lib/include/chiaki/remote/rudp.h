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
#include <chiaki/common.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Handle to rudp session state */
typedef struct rudp_t* Rudp;
typedef struct rudp_message_t RudpMessage;

typedef enum rudp_packet_type_t
{
    INIT_REQUEST = 0x8030,
    INIT_RESPONSE = 0xD00,
    COOKIE_REQUEST = 0x9030,
    COOKIE_RESPONSE = 0xa030,
    SESSION_MESSAGE = 0x2030,
    TAKION_SWITCH_ACK = 0x242E,
    ACK = 0x2430,
    CTRL_MESSAGE = 0x2430,
    UNKNOWN = 0x022F,
    FINISH = 0xC00,
} RudpPacketType;

/** Rudp Message */
struct rudp_message_t
{
    RudpPacketType type;
    uint16_t size;
    uint8_t *data;
    size_t data_size;
    RudpMessage *subMessage;
};

/**
 * Create rudp instance
 *
 * @param[in] rudp The Rudp instance to be initialized
 * @param[in] log The ChiakiLog to use for log messages\
*/
CHIAKI_EXPORT void init_rudp(
Rudp rudp, ChiakiLog *log);

/**
 * Serializes rudp message into byte array
 *
 * @param[in] rudp The Rudp instance to be initialized
 * @param[in] message The rudp message to serialize
 * @param[out] serialized_msg The serialized message
 * @param[out] msg_size The size of the serialized message
 * 
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
*/
CHIAKI_EXPORT ChiakiErrorCode rudp_message_serialize(
    Rudp rudp, RudpMessage *message, uint8_t *serialized_msg, size_t *msg_size);

/**
 * Serializes rudp message into byte array
 *
 * @param[in] rudp The Rudp instance to be initialized
 * @param[in] serialized_msg The serialized message to transform into a rudp message
 * @param[in] msg_size The size of the serialized message
 * @param[out] message The rudp messaage created from the serialized message
 * 
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
*/
CHIAKI_EXPORT ChiakiErrorCode rudp_message_parse(
    Rudp rudp, uint8_t *serialized_msg, size_t msg_size, RudpMessage *message);

/**
 * Frees rudp message data
 *
 * @param message The Rudp message with the data to free
*/
CHIAKI_EXPORT void rudp_message_free(RudpMessage *message);

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