#ifndef CHIAKI_ENTER_PIN_VIEW_H
#define CHIAKI_ENTER_PIN_VIEW_H
#include <borealis.hpp>
#include "host.h"

class EnterPinView :public brls::View
{
	private:
		Host *host;
		std::string login_pin;
		Settings *settings;
		ChiakiLog *log = nullptr;
		bool isError = false;
	public:
		EnterPinView(Host *host, bool isError);
		~EnterPinView();
		void ClosePinView();
		void draw(NVGcontext *vg, int x, int y, unsigned width, unsigned height, brls::Style *style, brls::FrameContext *ctx) override;
};
#endif // CHIAKI_ENTER_PIN_VIEW_H