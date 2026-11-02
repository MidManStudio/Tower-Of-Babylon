// Stand-in for Emscripten's header, for tests/platform_check.sh only: enough for the web branches to be compiled on a PC.
#ifndef STUB_EMSCRIPTEN_H
#define STUB_EMSCRIPTEN_H
#define EM_JS(ret, name, params, ...) ret name params;
typedef void (*em_callback_func)(void);
void emscripten_set_main_loop(em_callback_func func, int fps, int simulate_infinite_loop);
#endif
