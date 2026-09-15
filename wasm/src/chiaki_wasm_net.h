#ifndef CHIAKI_WASM_NET_H
#define CHIAKI_WASM_NET_H

#ifdef __cplusplus
extern "C" {
#endif

int chiaki_wasm_net_connect(const char *proxy_url);
void chiaki_wasm_net_disconnect(void);
int chiaki_wasm_net_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif
