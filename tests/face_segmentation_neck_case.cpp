#include <cstdio>
#include <vector>
#include "FaceSegmentation.h"

int main()
{
    const int width = 160, height = 120;
    std::vector<unsigned char> u(width * height, 0);
    std::vector<unsigned char> v(width * height, 0);
    for (int y = 18; y < 57; ++y)
        for (int x = 63; x < 97; ++x)
        {
            u[y * width + x] = 100;
            v[y * width + x] = 145;
        }
    for (int y = 57; y < 76; ++y)
        for (int x = 73; x < 87; ++x)
        {
            u[y * width + x] = 100;
            v[y * width + x] = 145;
        }
    for (int y = 76; y < 102; ++y)
        for (int x = 42; x < 118; ++x)
        {
            u[y * width + x] = 100;
            v[y * width + x] = 145;
        }

    const FaceSegmentation::Result baseline =
        FaceSegmentation::RunBaseline(u.data(), v.data(), width, height);
    if (baseline.left != 84 || baseline.top != 18 ||
        baseline.width != 152 || baseline.height != 84 ||
        baseline.mask[90 * width + 50] != 255)
    {
        std::fprintf(stderr, "unexpected baseline box: %d %d %d %d\n",
            baseline.left, baseline.top, baseline.width, baseline.height);
        return 1;
    }
    const FaceSegmentation::Result refined =
        FaceSegmentation::RunRefined(u.data(), v.data(), width, height);
    if (refined.left != 126 || refined.top != 18 ||
        refined.width != 68 || refined.height != 39 ||
        refined.pixelCount != 2652 ||
        refined.mask[90 * width + 50] != 0 ||
        refined.mask[35 * width + 80] != 255)
    {
        std::fprintf(stderr, "unexpected refined box/count: %d %d %d %d %d\n",
            refined.left, refined.top, refined.width, refined.height,
            refined.pixelCount);
        return 1;
    }
    for (int y = 57; y < 76; ++y)
        for (int x = 0; x < width; ++x)
        {
            const bool skin = x >= 70 && x < 90;
            u[y * width + x] = skin ? 100 : 0;
            v[y * width + x] = skin ? 145 : 0;
        }
    const FaceSegmentation::Result shallow =
        FaceSegmentation::RunRefined(u.data(), v.data(), width, height);
    if (shallow.left != 126 || shallow.top != 18 ||
        shallow.width != 68 || shallow.height != 58 ||
        shallow.mask[70 * width + 80] != 255 ||
        shallow.mask[90 * width + 50] != 0)
    {
        std::fprintf(stderr, "unexpected shallow-neck box: %d %d %d %d\n",
            shallow.left, shallow.top, shallow.width, shallow.height);
        return 1;
    }
    std::puts("Connected head-neck-shoulders refinement: PASS");
    return 0;
}
