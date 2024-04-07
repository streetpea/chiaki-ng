#import <AVFoundation/AVFoundation.h>
#import "macMicPermission.h"

void macMicPermission(MacMicPermissionCb cb, void *user) {

    // Request permission to access microphone.
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio])
    {
        case AVAuthorizationStatusAuthorized:
        {
            cb(AUTHORIZED, user);
            break;
        }
        case AVAuthorizationStatusNotDetermined:
        {
            // If the system hasn't determined the user's authorization status,
            // explicitly prompt them for approval.
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
                if (granted) {
                    cb(AUTHORIZED, user);
                }
                else
                   cb(DENIED, user);
            }];
            break;
        }
        case AVAuthorizationStatusDenied:
        {
            // The user has previously denied access.
            cb(DENIED, user);
            break;
        }
        case AVAuthorizationStatusRestricted:
        {
            // The user can't grant access due to restrictions.
            cb(RESTRICTED, user);
            break;
        }
    }
}