#include <chiaki/remote/rudp.h>
#include <chiaki/sock.h>
#include <math.h>
#include <arpa/inet.h>
#include <string.h>

#define RUDP_CONSTANT 0x244F244F
typedef struct rudp_t
{
    uint16_t counter;
} RudpInstance;

static void increase_counter(RudpInstance *rudp);


CHIAKI_EXPORT void init_rudp(
    RudpInstance *rudp, ChiakiLog *log)
{
    rudp = calloc(1, sizeof(RudpInstance));
    rudp->counter = 0;
}

CHIAKI_EXPORT ChiakiErrorCode rudp_message_serialize(
    RudpInstance *rudp, RudpMessage *message, uint8_t *serialized_msg, size_t *msg_size)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    *(chiaki_unaligned_uint16_t *)(serialized_msg) = htons(message->size);
    *(chiaki_unaligned_uint32_t *)(serialized_msg + 2) = htonl(RUDP_CONSTANT);
    *(chiaki_unaligned_uint16_t *)(serialized_msg + 6) = htons(message->type);
    memcpy(serialized_msg + 8, message->data, message->data_size);
    *msg_size += 8 + sizeof(message->data);
    if(message->subMessage)
    {
        err = rudp_message_serialize(rudp, message->subMessage, serialized_msg + 8 + message->data_size, msg_size);
    }
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode rudp_message_parse(
    RudpInstance *rudp, uint8_t *serialized_msg, size_t msg_size, RudpMessage *message)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    message->data = NULL;
    message->subMessage = NULL;
    message->size = ntohs(*(chiaki_unaligned_uint16_t *)(serialized_msg));
    message->type = ntohs(*(chiaki_unaligned_uint16_t *)(serialized_msg + 6));
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
    }


    remaining = remaining - subMessageDataLeft;
    if (remaining >= 8)
    {
        message->subMessage = calloc(1, sizeof(RudpMessage));
        err = rudp_message_parse(rudp, serialized_msg + 8 + subMessageDataLeft, remaining, message->subMessage);
    }
    return err;
}

CHIAKI_EXPORT void rudp_message_free(RudpMessage *message)
{
    if(message->data)
        free(message->data);
    if(message->subMessage)
        free(message->subMessage);
}

CHIAKI_EXPORT void chiaki_rudp_fini(RudpInstance *rudp)
{
    if(rudp)
        free(rudp);
}

static void increase_counter(RudpInstance *rudp)
{
    if(rudp->counter >= UINT16_MAX)
        rudp->counter = 0;
    else
        rudp->counter++;
}

