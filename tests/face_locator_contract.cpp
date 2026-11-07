#include <windows.h>
#include <algorithm>
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
        bs->rcnFace.width != 40 || bs->rcnFace.height >= 24 ||
        bs->nFacePixelNum <= 0 || bs->nFacePixelNum >= 960 ||
        !bs->TempImage1d8[30 * (w / 4) + 50] ||
        bs->TempImage1d8[45 * (w / 4) + 50] ||
        std::memcmp(originalUV.data(), bs->clrBmp_1d8 + yCount,
                    uvCount * 2) != 0)
    {
        std::fprintf(stderr, "basic contract: box=%d,%d,%d,%d pixels=%d\n",
            bs->rcnFace.left, bs->rcnFace.top, bs->rcnFace.width,
            bs->rcnFace.height, bs->nFacePixelNum);
        return 1;
    }
    // 用头颈连通样本确认 DLL 实际采用稳定脸宽驱动的下颚曲线。
    std::fill(y, y + pixels, 150);
    std::memset(u, 0, pixels);
    auto fillSkin = [&](int left, int top, int right, int bottom)
    {
        for (int row = top; row < bottom; ++row)
            for (int col = left; col < right; ++col)
            {
                const int index = (row * 4) * (w / 2) + col * 2;
                u[index] = 100;
                v[index] = 145;
            }
    };
    fillSkin(70, 18, 90, 30);
    fillSkin(63, 30, 97, 57);
    fillSkin(71, 57, 88, 85);
    std::fill(y + 42 * 4 * w, y + (42 * 4 + 1) * w, 90);
    std::fill(shared.begin(), shared.end(), 0);
    ((BUF_STRUCT*)shared.data())->bNotInited = true;
    runPrepare(w, h, y, u, v, shared.data());
    bs = (BUF_STRUCT*)shared.data();
    std::memcpy(originalUV.data(), bs->clrBmp_1d8 + yCount, uvCount * 2);
    runLocate(w, h, y, u, v, shared.data());
    if (!bs->TempImage1d8[45 * (w / 4) + 80] ||
        bs->TempImage1d8[68 * (w / 4) + 80] ||
        bs->rcnFace.height >= 67 ||
        std::memcmp(originalUV.data(), bs->clrBmp_1d8 + yCount,
                    uvCount * 2) != 0)
    {
        std::fprintf(stderr, "jaw contract: box=%d,%d,%d,%d kept_head=%d kept_neck=%d\n",
            bs->rcnFace.left, bs->rcnFace.top, bs->rcnFace.width,
            bs->rcnFace.height, bs->TempImage1d8[45 * (w / 4) + 80] != 0,
            bs->TempImage1d8[68 * (w / 4) + 80] != 0);
        return 1;
    }

    // 过窄连通域不能形成可信脸宽，应完整回退到肤色基线。
    std::fill(y, y + pixels, 150);
    std::memset(u, 0, pixels);
    fillSkin(75, 20, 85, 50);
    std::fill(shared.begin(), shared.end(), 0);
    ((BUF_STRUCT*)shared.data())->bNotInited = true;
    runPrepare(w, h, y, u, v, shared.data());
    bs = (BUF_STRUCT*)shared.data();
    runLocate(w, h, y, u, v, shared.data());
    if (bs->rcnFace.left != 150 || bs->rcnFace.top != 20 ||
        bs->rcnFace.width != 20 || bs->rcnFace.height != 30 ||
        bs->nFacePixelNum != 600 ||
        !bs->TempImage1d8[20 * (w / 4) + 75] ||
        !bs->TempImage1d8[49 * (w / 4) + 84])
    {
        std::fprintf(stderr, "baseline fallback: box=%d,%d,%d,%d pixels=%d\n",
            bs->rcnFace.left, bs->rcnFace.top, bs->rcnFace.width,
            bs->rcnFace.height, bs->nFacePixelNum);
        return 1;
    }
    std::puts("FaceLocator plugin contract: PASS");
    FreeLibrary(locate);
    FreeLibrary(prepare);
    return 0;
}
