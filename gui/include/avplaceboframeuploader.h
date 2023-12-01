#ifndef CHIAKI_AVPLACEBOFRAMEUPLOADER_H
#define CHIAKI_AVPLACEBOFRAMEUPLOADER_H

#include <QObject>

class StreamSession;
class AVPlaceboWidget;

class AVPlaceboFrameUploader: public QObject
{
    Q_OBJECT

    private:
        StreamSession *session;
        AVPlaceboWidget *widget;

    private slots:
        void UpdateFrameFromDecoder();

    public:
        AVPlaceboFrameUploader(StreamSession *session, AVPlaceboWidget *widget);
};
#endif // CHIAKI_AVPLACEBOFRAMEUPLOADER_H