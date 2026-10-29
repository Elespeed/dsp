#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "BufStruct.h"

typedef void (__cdecl *PluginRun)(int, int, BYTE*, BYTE*, BYTE*, BYTE*);

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: image_prepare_contract <PA_ImagePrepare.dll>\n");
        return 2;
    }
    HMODULE module = LoadLibraryA(argv[1]);
    if (!module)
    {
        std::fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError());
        return 2;
    }
    PluginRun run = (PluginRun)GetProcAddress(module, "ON_PLUGINRUN");
    if (!run)
        return 2;

    const int w = 640, h = 480;
    const int pixels = w * h;
    std::vector<BYTE> y(pixels), u(pixels / 2), v(pixels / 2);
    for (int row = 0; row < h; ++row)
    {
        for (int col = 0; col < w; ++col)
            y[row * w + col] = (BYTE)((col + 3 * row) & 255);
        for (int col = 0; col < w / 2; ++col)
        {
            u[row * w / 2 + col] = (BYTE)((2 * col + row) & 255);
            v[row * w / 2 + col] = (BYTE)((col + 5 * row) & 255);
        }
    }
    const std::vector<BYTE> originalY = y, originalU = u, originalV = v;
    std::vector<BYTE> shared(pixels * 4, 0xA5);
    ((BUF_STRUCT*)shared.data())->bNotInited = true;
    run(w, h, y.data(), u.data(), v.data(), shared.data());

    BUF_STRUCT* bs = (BUF_STRUCT*)shared.data();
    if (bs->bNotInited || bs->W != w || bs->H != h ||
        !bs->clrBmp_1d8 || !bs->pOtherData ||
        bs->clrBmp_1d8 < shared.data() ||
        bs->clrBmp_1d8 + pixels / 4 > shared.data() + shared.size() ||
        bs->pOtherData > shared.data() + shared.size())
        return 1;

    const int smallW = w / 2, smallH = h / 4;
    const int yCount = smallW * smallH;
    const int uvCount = yCount / 2;
    const BYTE* outY = bs->clrBmp_1d8;
    const BYTE* outU = outY + yCount;
    const BYTE* outV = outU + uvCount;
    for (int row = 0; row < smallH; ++row)
    {
        for (int col = 0; col < smallW; ++col)
            if (outY[row * smallW + col] != originalY[(row * 4) * w + col * 2])
                return 1;
        for (int col = 0; col < smallW / 2; ++col)
        {
            const int source = (row * 4) * (w / 2) + col * 2;
            const int target = row * (smallW / 2) + col;
            if (outU[target] != originalU[source] || outV[target] != originalV[source])
                return 1;
        }
    }
    if (y != originalY || u != originalU || v != originalV ||
        std::memcmp(bs->colorBmp, originalY.data(), pixels) != 0 ||
        std::memcmp(bs->colorBmp + pixels, originalU.data(), pixels / 2) != 0 ||
        std::memcmp(bs->colorBmp + pixels + pixels / 2, originalV.data(), pixels / 2) != 0)
        return 1;
    std::puts("ImagePrepare buffer and sampling contract: PASS");
    FreeLibrary(module);
    return 0;
}
