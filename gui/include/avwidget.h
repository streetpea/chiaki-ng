// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_AVWIDGET_H
#define CHIAKI_AVWIDGET_H

class IAVWidget  {
public:
    virtual ~IAVWidget() {};

    virtual void Stop() = 0;
    virtual void ToggleStretch() = 0;
    virtual void ToggleZoom() = 0;
};

enum ResolutionMode {Normal = 0, Zoom = 1, Stretch = 2};

#endif // CHIAKI_AVWIDGET_H