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
#include <chiaki/sock.h>
#include <chiaki/stoppipe.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Handle to rudp session state */
typedef struct rudp_t* ChiakiRudp;
typedef struct rudp_message_t RudpMessage;

typedef enum rudp_packet_type_t
{
    INIT_REQUEST = 0x8030,
    INIT_RESPONSE = 0xD000,
    COOKIE_REQUEST = 0x9030,
    COOKIE_RESPONSE = 0xA030,
    SESSION_MESSAGE = 0x2030,
    TAKION_SWITCH_ACK = 0x242E,
    ACK = 0x2430,
    CTRL_MESSAGE = 0x0230,
    UNKNOWN = 0x022F,
    OFFSET8 = 0x1230,
    OFFSET10 = 0x2630,
    FINISH = 0xC000,
} RudpPacketType;

/** Rudp Message */
struct rudp_message_t
{
    uint8_t subtype;
    RudpPacketType type;
    uint16_t size;
    uint8_t *data;
    size_t data_size;
    uint16_t remote_counter;
    RudpMessage *subMessage;
    uint16_t subMessage_size;
};

/**
 * Create rudp instance
 *
 * @param[in] sock Pointer to a sock to use for Rudp messages
 * @param[in] log The ChiakiLog to use for log messages
 * @return The initialized rudp instance
*/
CHIAKI_EXPORT ChiakiRudp chiaki_rudp_init(chiaki_socket_t *sock,
    ChiakiLog *log);

/**
 * Creates and sends an init rudp message for use when starting the session
 *
 * @param rudp Pointer to the Rudp instance to use for the message
 * @param[out] local_counter The local counter used for the message
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
*/

/**
 * Resets the counter and header of the rudp instance (used before init message is sent if rudp is already initialized)
 *
 * @param rudp Pointer to the Rudp instance of which to reset the counter and header
*/
CHIAKI_EXPORT void chiaki_rudp_reset_counter_header(ChiakiRudp rudp);

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_init_message(
    ChiakiRudp rudp, uint16_t *local_counter);

/**
 * Creates and sends a cookie rudp message for use after starting message
 *
 * @param rudp Pointer to the Rudp instance to use for the message
 * @param[in] response_buf The response from the init message
 * @param[in] response_size The size of the response from the init message
 * @param[out] local_counter The local counter used for the message
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
 * 
*/
CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_cookie_message(ChiakiRudp rudp, uint8_t *response_buf, 
    size_t response_size, uint16_t *local_counter);

/**
 * Creates and sends a session rudp message for use with registration message
 *
 * @param rudp Pointer to the Rudp instance to use for the message
 * @param[in] session_msg The data from the session msg (i.e., regist message)
 * @param[in] session_msg_sizse The size of the session message
 * @param[out] local_counter The local counter used for the message
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
 * 
*/
CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_session_message(ChiakiRudp rudp, uint16_t remote_counter,
    uint8_t *session_msg, size_t session_msg_size, uint16_t *local_counter);

/**
 * Creates and sends an ack rudp message for use in acking received rudp messages
 *
 * @param rudp Pointer to the Rudp instance to use for the message
 * @param[in] remote_counter The remote counter of the message to ack
 * @param[in] resend Whether the package should be added to the resend buffer
 * @param[out] local_counter Pointer to the local counter used for the message or NULL if not needed
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
*/
CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_ack_message(ChiakiRudp rudp, uint16_t remote_counter, bool resend, uint16_t *local_counter);

/**
 * Creates and sends a ctrl rudp message for use with ctrl
 *
 * @param rudp Pointer to the Rudp indstance to use for the message
 * @param[in] ctrl_message The byte array of the ctrl message to send
 * @param[in] ctrl_messaage_size The size of the ctrl message to send
 * @param[in] resend Whether the package should be added to the resend buffer
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
 * 
*/
CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_ctrl_message(ChiakiRudp rudp, uint8_t *ctrl_message, 
    size_t ctrl_message_size, bool resend);

/**
 * Creates and sends a switch to takion rudp message for use when switching to takion
 *
 * @param rudp Pointer to the Rudp indstance to use for the message
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
*/
CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_switch_to_takion_message(ChiakiRudp rudp);

/**
 * Parses byte array into rudp message
 *
 * @param[in] serialized_msg The serialized message to transform into a rudp message
 * @param[in] msg_size The size of the serialized message
 * @param[out] message The rudp messaage created from the serialized message
 * 
*/

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_raw(ChiakiRudp rudp,
    uint8_t *buf, size_t buf_size);

/**
 * Selects an incoming message from the queue and receives the rudp message
 *
 * @param rudp Pointer to the Rudp instance to use
 * @param[in] buf_size The size of the received message
 * @param[out] message The Rudp message returned
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
 *
*/
CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_select_recv(ChiakiRudp rudp, size_t buf_size, RudpMessage *message);

/**
 * Receives the rudp message. Must use select separately from this function
 *
 * @param rudp Pointer to the Rudp instance to use
 * @param[in] buf_size The size of the received message
 * @param[out] message The Rudp message returned
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
 * 
*/
CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_recv_only(ChiakiRudp rudp, size_t buf_size,  RudpMessage *message);

/**
 * Selects a rudp message using the given stop pipe and timeout
 *
 * @param rudp Pointer to the Rudp instance to use
 * @param[in] stop_pipe Pointer to the stop pipe to use
 * @param[in] timeout The timeout in ms
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
 *
*/
CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_stop_pipe_select_single(ChiakiRudp rudp, ChiakiStopPipe *stop_pipe, uint64_t timeout);

/**
 * Frees rudp message pointers if present (data and subMessage)
 *
 * @param message Pointer to the Rudp message with the data to free
*/

/**
 * Acknowledge received ack for rudp packet
 *
 * @param rudp Pointer to the Rudp instance to use
 * @param[in] counter_to_ack The size of the received message
 * @param[out] message The Rudp message returned
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
 * 
*/

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_ack_packet(ChiakiRudp rudp, uint16_t counter_to_ack);

/**
 * Print Rudp Message
 *
 * @param rudp Pointer to the Rudp instance to use
 * @param[in] message Pointer to the message to print
 * 
*/
CHIAKI_EXPORT void chiaki_rudp_print_message(ChiakiRudp rudp, RudpMessage *message);

CHIAKI_EXPORT void chiaki_rudp_message_pointers_free(RudpMessage *message);

/**
 * Terminate rudp instance
 *
 * @param[in] rudp Pointer to the Rudp instance to be destroyed
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
*/
CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_fini(ChiakiRudp rudp);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_RUDP_H