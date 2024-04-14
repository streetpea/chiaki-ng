#include <chiaki/remote/rudp.h>

typedef struct rudp_t
{
    size_t counter;
} RudpInstance;

CHIAKI_EXPORT ChiakiErrorCode init_rudp(
    Rudp rudp, ChiakiLog *log)
{
    rudp = calloc(1, sizeof(RudpInstance));
    rudp->counter = 0;
}

CHIAKI_EXPORT void chiaki_rudp_fini(Rudp rudp)
{
    if(rudp != NULL)
        free(rudp);
}