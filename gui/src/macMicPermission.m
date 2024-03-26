#import <AVFoundation/AVFoundation.h>
#import "macMicPermission.h"

void macMicPermission(MacMicPermissionCb cb, void *user) {

    // Request permission to access microphone.
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio])
    {
        case AVAuthorizationStatusAuthorized:
        {
            cb(true, user);
            break;
        }
        case AVAuthorizationStatusNotDetermined:
        {
            // If the system hasn't determined the user's authorization status,
            // explicitly prompt them for approval.
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
                if (granted) {
                    cb(true, user);
                }
                else
                   cb(false, user);
            }];
            break;
        }
        case AVAuthorizationStatusDenied:
        {
            // The user has previously denied access.
            cb(false, user);
            break;
        }
        case AVAuthorizationStatusRestricted:
        {
            // The user can't grant access due to restrictions.
            cb(false, user);
            break;
        }
    }
}