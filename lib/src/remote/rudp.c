#include <chiaki/remote/rudp.h>
#include <chiaki/random.h>
#include <chiaki/thread.h>
#include <chiaki/stoppipe.h>
#include <chiaki/remote/rudpsendbuffer.h>

#include <math.h>
#include <string.h>
#include <assert.h>

#define RUDP_CONSTANT 0x244F244F
#define RUDP_SEND_BUFFER_SIZE 16
#define RUDP_EXPECT_TIMEOUT_MS 5000
typedef struct rudp_t
{
    uint16_t counter;
    uint32_t header;
    ChiakiMutex counter_mutex;
    ChiakiStopPipe stop_pipe;
    chiaki_socket_t sock;
    ChiakiLog *log;
    ChiakiRudpSendBuffer send_buffer;
} RudpInstance;

static uint16_t increase_counter(RudpInstance *rudp);
static void rudp_message_serialize(RudpMessage *message, uint8_t *serialized_msg, size_t *msg_size);


CHIAKI_EXPORT RudpInstance *chiaki_rudp_init(chiaki_socket_t *sock, ChiakiLog *log)
{
    RudpInstance *rudp = (RudpInstance *)calloc(1, sizeof(RudpInstance));
    rudp->log = log;
    rudp->counter = chiaki_random_32()%0x5E00 + 0x1FF;
    rudp->header = chiaki_random_32() + 0x8000;
    ChiakiErrorCode err;
    err = chiaki_mutex_init(&rudp->counter_mutex, false);
    assert(err == CHIAKI_ERR_SUCCESS);
    err = chiaki_stop_pipe_init(&rudp->stop_pipe);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(rudp->log, "Rudp failed initializing, failed creating stop pipe");
        return NULL;
    }
    rudp->sock = *sock;
	// The send buffer size MUST be consistent with the acked seqnums array size in rudp_handle_message_ack()
    err = chiaki_rudp_send_buffer_init(&rudp->send_buffer, rudp, log, RUDP_SEND_BUFFER_SIZE);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(rudp->log, "Rudp failed initializing, failed creating send buffer");
        return NULL;
    }
    return rudp;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_init_message(RudpInstance *rudp, uint16_t *local_counter)
{
    RudpMessage message;
    *local_counter = increase_counter(rudp);
    message.type = INIT_REQUEST;
    message.subMessage = NULL;
    message.data_size = 14;
    uint8_t data[message.data_size];
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = calloc(alloc_size, sizeof(uint8_t));
    size_t msg_size = 0;
    message.size = (0x0c << 12) | alloc_size;
    const uint8_t after_header[0x2] = { 0x05, 0x82 };
    const uint8_t after_counter[0x6] = { 0x0b, 0x01, 0x01, 0x00, 0x01, 0x00 };
    *(chiaki_unaligned_uint16_t *)(data) = htons(*local_counter);
    memcpy(data + 2, after_counter, sizeof(after_counter));
    *(chiaki_unaligned_uint16_t *)(data + 8) = htonl(rudp->header);
    memcpy(data + 12, after_header, sizeof(after_header));
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        free(serialized_msg);
        return err;
    }
    chiaki_log_hexdump(rudp->log, CHIAKI_LOG_INFO, serialized_msg, msg_size);
    err = chiaki_rudp_send_buffer_push(&rudp->send_buffer, *local_counter, serialized_msg, msg_size);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_cookie_message(RudpInstance *rudp, uint8_t *response_buf, size_t response_size, uint16_t *local_counter)
{
    RudpMessage message;
    *local_counter = increase_counter(rudp);
    message.type = COOKIE_REQUEST;
    message.subMessage = NULL;
    message.data_size = 14 + response_size;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = calloc(alloc_size, sizeof(uint8_t));
    size_t msg_size = 0;
    message.size = (0x0c << 12) | alloc_size;
    uint8_t data[message.data_size];
    const uint8_t after_header[0x2] = { 0x05, 0x82 };
    const uint8_t after_counter[0x6] = { 0x0b, 0x01, 0x01, 0x00, 0x01, 0x00 };
    *(chiaki_unaligned_uint16_t *)(serialized_msg) = htons(*local_counter);
    memcpy(data + 2, after_counter, sizeof(after_counter));
    *(chiaki_unaligned_uint16_t *)(data + 8) = htonl(rudp->header);
    memcpy(data + 12, after_header, sizeof(after_header));
    memcpy(data + 14, response_buf, response_size);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        free(serialized_msg);
        return err;
    }
    chiaki_log_hexdump(rudp->log, CHIAKI_LOG_INFO, serialized_msg, msg_size);
    err = chiaki_rudp_send_buffer_push(&rudp->send_buffer, *local_counter, serialized_msg, msg_size);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_session_message(RudpInstance *rudp, uint16_t remote_counter, uint8_t *session_msg, size_t session_msg_size, uint16_t *local_counter)
{
    RudpMessage subMessage;
    *local_counter = increase_counter(rudp);
    subMessage.type = CTRL_MESSAGE;
    subMessage.subMessage = NULL;
    subMessage.data_size = 2 + session_msg_size;
    uint8_t subdata[subMessage.data_size];
    *(chiaki_unaligned_uint16_t *)(subdata) = htons(*local_counter);
    memcpy(subdata + 2, session_msg, session_msg_size);
    subMessage.data = subdata;

    RudpMessage message;
    message.type = SESSION_MESSAGE;
    message.subMessage = &subMessage;
    message.data_size = 4;
    size_t alloc_size = 8 + message.data_size + session_msg_size;
    uint8_t *serialized_msg = calloc(alloc_size, sizeof(uint8_t));
    size_t msg_size = 0;
    message.size = (0x0c << 12) | alloc_size;
    uint8_t data[message.data_size];
    *(chiaki_unaligned_uint16_t *)(data) = htons(*local_counter);
    *(chiaki_unaligned_uint16_t *)(data + 2) = htons(remote_counter);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        free(serialized_msg);
        return err;
    }
    err = chiaki_rudp_send_buffer_push(&rudp->send_buffer, *local_counter, serialized_msg, msg_size);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_ack_message(RudpInstance *rudp, uint16_t remote_counter, uint16_t *local_counter)
{
    RudpMessage message;
    uint16_t counter = increase_counter(rudp);
    if(local_counter)
        *local_counter = counter;
    message.type = ACK;
    message.subMessage = NULL;
    message.data_size = 6;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = calloc(alloc_size, sizeof(uint8_t));
    size_t msg_size = 0;
    message.size = (0x0c << 12) | alloc_size;
    uint8_t data[message.data_size];
    const uint8_t after_counters[0x2] = { 0x00, 0x92 };
    *(chiaki_unaligned_uint16_t *)(data) = htons(counter);
    *(chiaki_unaligned_uint16_t *)(data + 2) = htons(remote_counter);
    memcpy(data + 4, after_counters, sizeof(after_counters));
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        free(serialized_msg);
        return err;
    }
    free(serialized_msg);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_ctrl_message(RudpInstance *rudp, uint8_t *ctrl_message, size_t ctrl_message_size)
{
    RudpMessage message;
    uint16_t counter = increase_counter(rudp);
    message.type = CTRL_MESSAGE;
    message.subMessage = NULL;
    message.data_size = 2 + ctrl_message_size;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = calloc(alloc_size, sizeof(uint8_t));
    size_t msg_size = 0;
    message.size = (0x0c << 12) | alloc_size;
    uint8_t data[message.data_size];
    *(chiaki_unaligned_uint16_t *)(data) = htons(counter);
    memcpy(data + 2, ctrl_message, ctrl_message_size);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        free(serialized_msg);
        return err;
    }
    err = chiaki_rudp_send_buffer_push(&rudp->send_buffer, counter, serialized_msg, msg_size);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_switch_to_takion_message(RudpInstance *rudp)
{
    RudpMessage message;
    uint16_t counter = increase_counter(rudp);
    message.type = CTRL_MESSAGE;
    message.subMessage = NULL;
    message.data_size = 26;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = calloc(alloc_size, sizeof(uint8_t));
    size_t msg_size = 0;
    message.size = (0x0c << 12) | alloc_size;
    uint8_t data[message.data_size];
    const size_t buf_size = 16;
    uint8_t buf[buf_size];
    const uint8_t before_buf[8] = { 0x00, 0x00, 0x00, 0x10, 0x00, 0x0d, 0x00, 0x00 };
    chiaki_random_bytes_crypt(buf, buf_size);
    *(chiaki_unaligned_uint16_t *)(data) = htons(counter);
    memcpy(data + 2, before_buf, sizeof(before_buf));
    memcpy(data + 10, buf, buf_size);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    uint8_t hmessage[22] = { 0xc0, 0x16, 0x24, 0x4f, 0x24, 0x4f, 0x80, 0x30, 0xf1, 0x3f, 0x0b, 0x01, 0x01, 0x00, 0x01, 0x00, 0x6c, 0x0d, 0xc1, 0x65, 0x05, 0x82 };
    memcpy(serialized_msg, hmessage, sizeof(hmessage));
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        free(serialized_msg);
        return err;
    }
    err = chiaki_rudp_send_buffer_push(&rudp->send_buffer, counter, serialized_msg, msg_size);
    return err;
}

/**
 * Serializes rudp message into byte array
 *
 * @param[in] message The rudp message to serialize
 * @param[out] serialized_msg The serialized message
 * @param[out] msg_size The size of the serialized message
 * 
*/
static void rudp_message_serialize(
    RudpMessage *message, uint8_t *serialized_msg, size_t *msg_size)
{
    *(chiaki_unaligned_uint16_t *)(serialized_msg) = htons(message->size);
    *(chiaki_unaligned_uint32_t *)(serialized_msg + 2) = htonl(RUDP_CONSTANT);
    *(chiaki_unaligned_uint16_t *)(serialized_msg + 6) = htons(message->type);
    memcpy(serialized_msg + 8, message->data, message->data_size);
    *msg_size += 8 + message->data_size;
    if(message->subMessage)
    {
        rudp_message_serialize(message->subMessage, serialized_msg + 8 + message->data_size, msg_size);
    }
}

/**
 * Send serialized rudp message across buffer
 *
 * @param rudp Pointer to the Rudp indstance to use
 * @param[in] buf The serialized Rudp message to send
 * @param[in] buf_size The size of the messsage to send
 * 
*/
static void chiaki_rudp_message_parse(
    uint8_t *serialized_msg, size_t msg_size, RudpMessage *message)
{
    message->data = NULL;
    message->subMessage = NULL;
    message->subMessage_size = 0;
    message->data_size = 0;
    message->size = ntohs(*(chiaki_unaligned_uint16_t *)(serialized_msg));
    message->type = ntohs(*(chiaki_unaligned_uint16_t *)(serialized_msg + 6));
    message->subtype = serialized_msg[6] & 0xFF;
    uint8_t length_array[2];
    uint16_t length;
    memcpy(length_array, serialized_msg, 2);
    length_array[0] = length_array[0] & 0x0F;
    length = ntohs(*(chiaki_unaligned_uint16_t *)(length_array));
    int remaining = msg_size - 8;
    int subMessageDataLeft = 0;
    if(length > 8)
    {
        int subMessageDataLeft = length - 8;
        subMessageDataLeft = fmin(subMessageDataLeft, remaining);
        message->data_size = subMessageDataLeft;
        message->data = calloc(message->data_size, sizeof(uint8_t));
        memcpy(message->data, serialized_msg, subMessageDataLeft);
        message->remote_counter = ntohs(*(chiaki_unaligned_uint16_t *)(message->data));
    }


    remaining = remaining - subMessageDataLeft;
    if (remaining >= 8)
    {
        message->subMessage = calloc(1, sizeof(RudpMessage));
        message->subMessage_size = remaining;
        chiaki_rudp_message_parse(serialized_msg + 8 + subMessageDataLeft, remaining, message->subMessage);
    }
}

/**
 * Increase rudp local counter
 *
 * @param[in] rudp The rudp instance to use
 * @return tmp The rudp counter before increasing
 * 
*/
static uint16_t increase_counter(RudpInstance *rudp)
{
    chiaki_mutex_lock(&rudp->counter_mutex);
    uint16_t tmp = rudp->counter;
    if(rudp->counter >= UINT16_MAX)
        rudp->counter = 0;
    else
        rudp->counter++;
    chiaki_mutex_unlock(&rudp->counter_mutex);
    return tmp;
}

CHIAKI_EXPORT uint16_t chiaki_rudp_get_local_counter(RudpInstance *rudp)
{
    return rudp->counter;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_raw(RudpInstance *rudp, uint8_t *buf, size_t buf_size)
{
    if(rudp->sock < 0)
    {
        return CHIAKI_ERR_DISCONNECTED;
    }
	int sent = send(rudp->sock, (CHIAKI_SOCKET_BUF_TYPE) buf, buf_size, 0);
	if(sent < 0)
	{
#ifdef _WIN32
		CHIAKI_LOGE(rudp->log, "Rudp raw failed to send packet: %u", WSAGetLastError());
#else
		CHIAKI_LOGE(rudp->log, "Rudp raw failed to send packet: %s", strerror(errno));
#endif
		return CHIAKI_ERR_NETWORK;
	}
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_recv(RudpInstance *rudp, size_t buf_size,  RudpMessage *message)
{
    uint8_t buf[buf_size]; 
	ChiakiErrorCode err = chiaki_stop_pipe_select_single(&rudp->stop_pipe, rudp->sock, false, RUDP_EXPECT_TIMEOUT_MS);
	if(err == CHIAKI_ERR_TIMEOUT || err == CHIAKI_ERR_CANCELED)
		return err;
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(rudp->log, "Rudp select failed: %s", strerror(errno));
		return err;
	}

	int received_sz = recv(rudp->sock, (CHIAKI_SOCKET_BUF_TYPE) buf, buf_size, 0);
	if(received_sz <= 8)
	{
		if(received_sz < 0)
			CHIAKI_LOGE(rudp->log, "Rudp recv failed: %s", strerror(errno));
		else
			CHIAKI_LOGE(rudp->log, "Rudp recv returned less than the required 8 byte RUDP header");
		return CHIAKI_ERR_NETWORK;
	}
    chiaki_log_hexdump(rudp->log, CHIAKI_LOG_INFO, buf, received_sz);

    chiaki_rudp_message_parse(buf, received_sz, message);
    
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_ack_packet(RudpInstance *rudp, uint16_t counter_to_ack)
{
	ChiakiSeqNum16 acked_seq_nums[RUDP_SEND_BUFFER_SIZE];
	size_t acked_seq_nums_count = 0;
	ChiakiErrorCode err = chiaki_rudp_send_buffer_ack(&rudp->send_buffer, counter_to_ack, acked_seq_nums, &acked_seq_nums_count);
    return err;
}

CHIAKI_EXPORT void chiaki_rudp_print_message(RudpInstance *rudp, RudpMessage *message)
{
    CHIAKI_LOGI(rudp->log, "-------------RUDP MESSAGE------------");
    RudpPacketType type = htons(message->type);
    switch(type)
    {
        case INIT_REQUEST:
            CHIAKI_LOGI(rudp->log, "Message Type: Init Request");
            break;
        case INIT_RESPONSE:
            CHIAKI_LOGI(rudp->log, "Message Type: Init Response");
            break;
        case COOKIE_REQUEST:
            CHIAKI_LOGI(rudp->log, "Message Type: Cookie Request");
            break;
        case COOKIE_RESPONSE:
            CHIAKI_LOGI(rudp->log, "Message Type: Cookie Response");
            break;
        case SESSION_MESSAGE:
            CHIAKI_LOGI(rudp->log, "Message Type: Session Message");
            break;
        case TAKION_SWITCH_ACK:
            CHIAKI_LOGI(rudp->log, "Message Type: Takion Switch Ack");
            break;
        case ACK:
            CHIAKI_LOGI(rudp->log, "Message Type: Ack");
            break;
        case CTRL_MESSAGE:
            CHIAKI_LOGI(rudp->log, "Message Type: Ctrl Message");
            break;
        case UNKNOWN:
            CHIAKI_LOGI(rudp->log, "Message Type: Unknown");
            break;
        case FINISH:
            CHIAKI_LOGI(rudp->log, "Message Type: Finish");
            break;
        default:
            CHIAKI_LOGI(rudp->log, "Unknown Message Type: %04x", type);
            break;      
    }
    CHIAKI_LOGI(rudp->log, "Rudp Message Subtype: %u", message->subtype);
    CHIAKI_LOGI(rudp->log, "Rudp Message Size: %u", message->size);
    CHIAKI_LOGI(rudp->log, "Rudp Message Data Size: %lu", message->data_size);
    CHIAKI_LOGI(rudp->log, "-----Rudp Message Data ---");
    chiaki_log_hexdump(rudp->log, CHIAKI_LOG_INFO, message->data, message->data_size);
    CHIAKI_LOGI(rudp->log, "Rudp Message Remote Counter: %u", message->remote_counter);
    if(message->subMessage)
        chiaki_rudp_print_message(rudp, message->subMessage);
}

CHIAKI_EXPORT void chiaki_rudp_message_pointers_free(RudpMessage *message)
{
    if(message->data)
        free(message->data);
    if(message->subMessage)
        free(message->subMessage);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_fini(RudpInstance *rudp)
{
    chiaki_rudp_send_buffer_fini(&rudp->send_buffer);
    CHIAKI_SOCKET_CLOSE(rudp->sock);
    ChiakiErrorCode err = chiaki_mutex_fini(&rudp->counter_mutex);
    chiaki_stop_pipe_fini(&rudp->stop_pipe);
    if(rudp)
        free(rudp);
    return err;
}

