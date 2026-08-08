#include <windows.h>

extern "C" void mcfix_fake_frame_handler() {}

BOOL WINAPI DllMain(HINSTANCE, DWORD, void*) {
    return TRUE;
}
