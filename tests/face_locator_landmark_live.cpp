#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "BufStruct.h"
#include "FaceSegmentation.h"

typedef void (__cdecl *PluginRun)(int, int, BYTE*, BYTE*, BYTE*, BYTE*);
typedef void (__cdecl *PluginExit)();

static int Clip(int value)
{
    return std::max(0, std::min(255, value));
}

static bool RunFrame(const char* filename, const char* output,
                     PluginRun prepare, PluginRun locate)
{
    std::ifstream stream(filename, std::ios::binary);
    BITMAPFILEHEADER file = {};
    BITMAPINFOHEADER info = {};
    stream.read(reinterpret_cast<char*>(&file), sizeof(file));
    stream.read(reinterpret_cast<char*>(&info), sizeof(info));
    if (!stream || file.bfType != 0x4d42 || info.biWidth != 640 ||
        info.biHeight != 480 || info.biBitCount != 24 ||
        info.biCompression != BI_RGB)
        return false;
    stream.seekg(file.bfOffBits);
    const int width = 640, height = 480, pixels = width * height;
    std::vector<BYTE> input(pixels * 2, 128);
    BYTE* y = input.data();
    BYTE* u = y + pixels;
    BYTE* v = u + pixels / 2;
    std::vector<BYTE> scanline(width * 3);
    for (int row = height - 1; row >= 0; --row)
    {
        stream.read(reinterpret_cast<char*>(scanline.data()), scanline.size());
        if (!stream)
            return false;
        for (int col = 0; col < width; ++col)
        {
            const int blue = scanline[col * 3];
            const int green = scanline[col * 3 + 1];
            const int red = scanline[col * 3 + 2];
            y[row * width + col] = static_cast<BYTE>(Clip(
                static_cast<int>(0.299 * red + 0.587 * green + 0.114 * blue)));
            if (col % 2 == 0)
            {
                const int index = row * (width / 2) + col / 2;
                u[index] = static_cast<BYTE>(Clip(static_cast<int>(
                    -0.1687 * red - 0.3313 * green + 0.5 * blue + 128)));
                v[index] = static_cast<BYTE>(Clip(static_cast<int>(
                    0.5 * red - 0.4187 * green - 0.0813 * blue + 128)));
            }
        }
    }
    std::vector<BYTE> shared(pixels * 4, 0);
    reinterpret_cast<BUF_STRUCT*>(shared.data())->bNotInited = true;
    prepare(width, height, y, u, v, shared.data());
    BUF_STRUCT* bs = reinterpret_cast<BUF_STRUCT*>(shared.data());
    const int yCount = (width / 2) * (height / 4);
    const int uvCount = (width / 4) * (height / 4);
    const BYTE* reducedU = bs->clrBmp_1d8 + yCount;
    const BYTE* reducedV = reducedU + uvCount;
    const FaceSegmentation::Result current = FaceSegmentation::RunRefined(
        reducedU, reducedV, width / 4, height / 4);
    locate(width, height, y, u, v, shared.data());
    std::ofstream mask(output, std::ios::binary);
    mask << "P5\n160 120\n255\n";
    mask.write(reinterpret_cast<char*>(bs->TempImage1d8), 160 * 120);
    std::printf("%s: current=(%d,%d,%d,%d) face=(%d,%d,%d,%d) pixels=%d\n", filename,
                current.left, current.top, current.width, current.height,
                bs->rcnFace.left, bs->rcnFace.top,
                bs->rcnFace.width, bs->rcnFace.height,
                bs->nFacePixelNum);
    return mask.good() && bs->nFacePixelNum > 0;
}

int main(int argc, char** argv)
{
    if (argc < 5 || argc % 2 != 1)
        return 2;
    HMODULE prepareModule = LoadLibraryA(argv[1]);
    HMODULE locateModule = LoadLibraryA(argv[2]);
    if (!prepareModule || !locateModule)
        return 2;
    PluginRun prepare = reinterpret_cast<PluginRun>(
        GetProcAddress(prepareModule, "ON_PLUGINRUN"));
    PluginRun locate = reinterpret_cast<PluginRun>(
        GetProcAddress(locateModule, "ON_PLUGINRUN"));
    PluginExit exitPlugin = reinterpret_cast<PluginExit>(
        GetProcAddress(locateModule, "ON_PLUGINEXIT"));
    if (!prepare || !locate)
        return 2;
    for (int i = 3; i + 1 < argc; i += 2)
        if (!RunFrame(argv[i], argv[i + 1], prepare, locate))
            return 1;
    if (exitPlugin)
        exitPlugin();
    FreeLibrary(locateModule);
    FreeLibrary(prepareModule);
    return 0;
}
