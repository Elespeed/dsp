#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "BufStruct.h"

typedef void (__cdecl *PluginRun)(int, int, BYTE*, BYTE*, BYTE*, BYTE*);

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    HMODULE prepare = LoadLibraryA(argv[1]);
    HMODULE locate = LoadLibraryA(argv[2]);
    if (!prepare || !locate)
    {
        std::fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError());
        return 2;
    }
    PluginRun runPrepare = (PluginRun)GetProcAddress(prepare, "ON_PLUGINRUN");
    PluginRun runLocate = (PluginRun)GetProcAddress(locate, "ON_PLUGINRUN");
    if (!runPrepare || !runLocate)
        return 2;

    const int w = 640, h = 480, pixels = w * h;
    std::vector<BYTE> input(pixels * 2, 128);
    BYTE* y = input.data();
    BYTE* u = y + pixels;
    BYTE* v = u + pixels / 2;
    std::memset(u, 0, pixels);
    for (int row = 100; row < 196; ++row)
        for (int col = 80; col < 120; ++col)
        {
            u[row * (w / 2) + col] = 100;
            v[row * (w / 2) + col] = 145;
        }
    for (int row = 132; row < 140; ++row)
        for (int col = 94; col < 98; ++col)
            u[row * (w / 2) + col] = v[row * (w / 2) + col] = 0;
    for (int row = 280; row < 312; ++row)
        for (int col = 200; col < 216; ++col)
        {
            u[row * (w / 2) + col] = 100;
            v[row * (w / 2) + col] = 145;
        }
    std::vector<BYTE> shared(pixels * 4, 0);
    ((BUF_STRUCT*)shared.data())->bNotInited = true;
    runPrepare(w, h, y, u, v, shared.data());
    BUF_STRUCT* bs = (BUF_STRUCT*)shared.data();
    const int yCount = (w / 2) * (h / 4);
    const int uvCount = (w / 4) * (h / 4);
    std::vector<BYTE> originalUV(uvCount * 2);
    std::memcpy(originalUV.data(), bs->clrBmp_1d8 + yCount, uvCount * 2);
    runLocate(w, h, y, u, v, shared.data());

    if (bs->rcnFace.left != 80 || bs->rcnFace.top != 25 ||
        bs->rcnFace.width != 40 || bs->rcnFace.height != 24 ||
        bs->nFacePixelNum != 960 ||
        std::memcmp(originalUV.data(), bs->clrBmp_1d8 + yCount,
                    uvCount * 2) != 0)
        return 1;
    for (int row = 0; row < h / 4; ++row)
        for (int col = 0; col < w / 4; ++col)
        {
            const bool expected = col >= 40 && col < 60 &&
                                  row >= 25 && row < 49;
            if ((bs->TempImage1d8[row * (w / 4) + col] != 0) != expected)
                return 1;
        }
    std::puts("FaceLocator plugin contract: PASS");
    FreeLibrary(locate);
    FreeLibrary(prepare);
    return 0;
}
