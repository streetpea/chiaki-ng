#ifndef MAC_MIC_PERMISSION_H
#define MAC_MIC_PERMISSION_H

typedef enum authorization_t
{
    AUTHORIZED,
    DENIED,
    RESTRICTED
} Authorization;

typedef void (*MacMicPermissionCb)(Authorization authorization, void *user);

void macMicPermission(MacMicPermissionCb cb, void *user);

#endif