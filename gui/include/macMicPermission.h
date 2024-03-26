#ifndef MAC_MIC_PERMISSION_H
#define MAC_MIC_PERMISSION_H

typedef void (*MacMicPermissionCb)(bool authorized, void *user);

void macMicPermission(MacMicPermissionCb cb, void *user);

#endif