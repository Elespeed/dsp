#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "BufStruct.h"

typedef void (__cdecl *PluginRun)(int, int, BYTE*, BYTE*, BYTE*, BYTE*);
typedef void (WINAPIV *PluginBeLast)(bool);

static int Fail(const char* stage)
{
    std::fprintf(stderr, "ImagePrepare contract failed: %s\n", stage);
    return 1;
}

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
    PluginBeLast beLast = (PluginBeLast)GetProcAddress(module, "ON_PLUGIN_BELAST");
    if (!run || !beLast)
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
        bs->displayImage != y.data() ||
        !bs->clrBmp_1d8 || !bs->pOtherData ||
        bs->clrBmp_1d8 < shared.data() ||
        bs->clrBmp_1d8 + pixels / 4 > shared.data() + shared.size() ||
        bs->pOtherData > shared.data() + shared.size())
        return Fail("shared buffer layout");

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
                return Fail("reduced color Y sampling");
        for (int col = 0; col < smallW / 2; ++col)
        {
            const int source = (row * 4) * (w / 2) + col * 2;
            const int target = row * (smallW / 2) + col;
            if (outU[target] != originalU[source] || outV[target] != originalV[source])
                return Fail("reduced color U/V sampling");
        }
    }
    if (y != originalY || u != originalU || v != originalV ||
        std::memcmp(bs->colorBmp, originalY.data(), pixels) != 0 ||
        std::memcmp(bs->colorBmp + pixels, originalU.data(), pixels / 2) != 0 ||
        std::memcmp(bs->colorBmp + pixels + pixels / 2, originalV.data(), pixels / 2) != 0 ||
        std::memcmp(bs->grayBmp, originalY.data(), pixels) != 0)
        return Fail("full-size color/gray copies or input immutability");

    const int grayW = w / 4, grayH = h / 4;
    for (int row = 0; row < grayH; ++row)
        for (int col = 0; col < grayW; ++col)
            if (bs->grayBmp_1d16[row * grayW + col] !=
                originalY[(row * 4) * w + col * 4])
                return Fail("reduced gray sampling");

    // 作为最后一个插件运行时，画面右侧应同时出现彩图和真正的灰度图。
    y = originalY;
    u = originalU;
    v = originalV;
    beLast(true);
    run(w, h, y.data(), u.data(), v.data(), shared.data());
    const int colorLeft = w - smallW;
    for (int row = 0; row < smallH; ++row)
    {
        for (int col = 0; col < smallW; ++col)
            if (y[row * w + colorLeft + col] != outY[row * smallW + col])
            {
                std::fprintf(stderr, "color Y mismatch at (%d,%d): got=%u expected=%u\n",
                             col, row, y[row * w + colorLeft + col],
                             outY[row * smallW + col]);
                return Fail("color preview Y plane");
            }
        for (int col = 0; col < smallW / 2; ++col)
        {
            const int target = row * (w / 2) + colorLeft / 2 + col;
            const int source = row * (smallW / 2) + col;
            if (u[target] != outU[source] || v[target] != outV[source])
                return Fail("color preview U/V planes");
        }
    }
    const int grayLeft = w - grayW;
    const int grayTop = smallH;
    for (int row = 0; row < grayH; ++row)
    {
        for (int col = 0; col < grayW; ++col)
            if (y[(grayTop + row) * w + grayLeft + col] !=
                bs->grayBmp_1d16[row * grayW + col])
                return Fail("gray preview Y plane");
        for (int col = 0; col < grayW / 2; ++col)
        {
            const int target = (grayTop + row) * (w / 2) + grayLeft / 2 + col;
            if (u[target] != 128 || v[target] != 128)
                return Fail("gray preview neutral U/V planes");
        }
    }
    std::puts("ImagePrepare buffer and sampling contract: PASS");
    FreeLibrary(module);
    return 0;
}
