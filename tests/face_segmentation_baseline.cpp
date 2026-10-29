#include <cstdio>
#include <vector>
#include "FaceSegmentation.h"

int main()
{
    const int width = 160, height = 120;
    std::vector<unsigned char> u(width * height, 0);
    std::vector<unsigned char> v(width * height, 0);
    FaceSegmentation::Result empty =
        FaceSegmentation::RunBaseline(u.data(), v.data(), width, height);
    if (empty.pixelCount || empty.width || empty.height)
        return 1;

    for (int y = 25; y < 49; ++y)
        for (int x = 40; x < 60; ++x)
        {
            u[y * width + x] = 100;
            v[y * width + x] = 145;
        }
    for (int y = 33; y < 35; ++y)
        for (int x = 47; x < 49; ++x)
            u[y * width + x] = v[y * width + x] = 0;
    for (int y = 70; y < 78; ++y)
        for (int x = 100; x < 108; ++x)
        {
            u[y * width + x] = 100;
            v[y * width + x] = 145;
        }
    u[10 * width + 10] = 100;
    v[10 * width + 10] = 145;
    const std::vector<unsigned char> originalU = u, originalV = v;
    FaceSegmentation::Result face =
        FaceSegmentation::RunBaseline(u.data(), v.data(), width, height);
    if (face.left != 80 || face.top != 25 || face.width != 40 ||
        face.height != 24 || face.pixelCount != 960 ||
        u != originalU || v != originalV)
    {
        std::fprintf(stderr, "wrong box/count: %d %d %d %d %d\n",
            face.left, face.top, face.width, face.height, face.pixelCount);
        return 1;
    }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const bool expected = x >= 40 && x < 60 && y >= 25 && y < 49;
            if ((face.mask[y * width + x] != 0) != expected)
                return 1;
        }
    std::puts("FaceSegmentation baseline: PASS");
    return 0;
}
