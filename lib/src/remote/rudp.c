#include <chiaki/remote/rudp.h>
#include <chiaki/sock.h>
#include <chiaki/common.h>
#include <math.h>

#define RUDP_CONSTANT 0x244F244F
typedef struct rudp_t
{
    uint16_t counter;
} RudpInstance;

typedef struct rudp_message_t
{
    RudpPacketType type;
    uint16_t size;
    uint8_t *data;
    size_t data_size;
    RudpMessage subMessage;
} RudpMessage;

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

static void increase_counter(RudpInstance *rudp);
static ChiakiErrorCode rudp_message_serialize(RudpInstance *rudp, RudpMessage *message, char *serialized_msg, size_t *msg);
static ChiakiErrorCode rudp_message_parse(RudpInstance *rudp, uint8_t *serialized_msg, size_t msg_size, RudpMessage *message);


CHIAKI_EXPORT ChiakiErrorCode init_rudp(
    RudpInstance *rudp, ChiakiLog *log)
{
    rudp = calloc(1, sizeof(RudpInstance));
    rudp->counter = 0;
}

CHIAKI_EXPORT ChiakiErrorCode rudp_message_serialize(
    RudpInstance *rudp, RudpMessage *message, char *serialized_msg, size_t *msg_size)
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
        subMessageDataLeft = min(subMessageDataLeft, remaining);
        message->data_size = subMessageDataLeft;
        calloc(message->data_size, sizeof(uint8_t));
        memcpy(message->data, serialized_msg, subMessageDataLeft);
        message->data_size = subMessageDataLeft;
    }


    remaining = remaining - subMessageDataLeft;
    if (remaining >= 8)
    {
        err = rudp_message_deserialize(rudp, serialized_msg + 8 + subMessageDataLeft , remaining, &message->subMessage);
    }
    return err;
}

CHIAKI_EXPORT void rudp_message_free_data(RudpMessage *message)
{
    if(message->data)
        free(message->data);
}

CHIAKI_EXPORT void chiaki_rudp_fini(RudpInstance *rudp)
{
    if(rudp != NULL)
        free(rudp);
}

static void increase_counter(RudpInstance *rudp)
{
    if(rudp->counter >= UINT16_MAX)
        rudp->counter = 0;
    else
        rudp->counter++;
}

