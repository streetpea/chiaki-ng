#include "views/ps_remote_play.h"
PSRemotePlay::PSRemotePlay(Host *host)
	: host(host)
{
	this->io = IO::GetInstance();
}

void PSRemotePlay::draw(NVGcontext *vg, int x, int y, unsigned width, unsigned height, brls::Style *style, brls::FrameContext *ctx)
{
	this->io->MainLoop();
	this->host->SendFeedbackState();
}

PSRemotePlay::~PSRemotePlay()
{
}
