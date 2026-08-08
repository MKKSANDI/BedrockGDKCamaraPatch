#include <windows.h>

#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::cerr << "usage: proxy_host <absolute-proxy-path>\n";
        return 2;
    }
    const HMODULE proxy = LoadLibraryExW(
        argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (proxy == nullptr) {
        std::cerr << "proxy load failed: " << GetLastError() << '\n';
        return 3;
    }
    const auto handler = GetProcAddress(proxy, "__CxxFrameHandler4");
    if (handler == nullptr) {
        std::cerr << "proxy export is missing\n";
        FreeLibrary(proxy);
        return 4;
    }
    FreeLibrary(proxy);
    std::cout << "runtime proxy loaded with required export\n";
    return 0;
}
