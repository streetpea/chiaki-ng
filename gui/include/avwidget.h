// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_AVWIDGET_H
#define CHIAKI_AVWIDGET_H

#include <QString>

class IAVWidget  {
public:
    virtual ~IAVWidget() {};

    virtual void Stop() = 0;
    virtual void ToggleStretch() = 0;
    virtual void ToggleZoom() = 0;
    virtual bool ShowError(const QString &title, const QString &text) { return false; }
};

enum ResolutionMode {Normal = 0, Zoom = 1, Stretch = 2};

#endif // CHIAKI_AVWIDGET_H
