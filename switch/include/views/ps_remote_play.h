#ifndef CHIAKI_PS_REMOTE_PLAY_H
#define CHIAKI_PS_REMOTE_PLAY_H

#include <borealis.hpp>
#include "host.h"
#include "io.h"

class PSRemotePlay : public brls::View
{
	private:
		brls::AppletFrame *frame;
		// to display stream on screen
		IO *io;
		// to send gamepad inputs
		Host *host;
		brls::Label *label;
	public:
		PSRemotePlay(Host *host);
		~PSRemotePlay();

		void draw(NVGcontext *vg, int x, int y, unsigned width, unsigned height, brls::Style *style, brls::FrameContext *ctx) override;
};


#endif // CHIAKI_PS_REMOTE_PLAY_H