#include <cstdio>
#include <vector>
#include "FaceSegmentation.h"

static std::vector<unsigned char> SlowMorph(const std::vector<unsigned char>& input,
                                            int width, int height, int radius,
                                            bool erode)
{
    std::vector<unsigned char> result(input.size(), 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            bool hit = erode;
            for (int dy = -radius; dy <= radius; ++dy)
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    const int xx = x + dx, yy = y + dy;
                    const bool value = xx >= 0 && xx < width &&
                        yy >= 0 && yy < height && input[yy * width + xx] != 0;
                    if (erode && !value)
                        hit = false;
                    if (!erode && value)
                        hit = true;
                }
            result[y * width + x] = hit ? 255 : 0;
        }
    return result;
}

int main()
{
    unsigned int state = 0x5eed1234U;
    for (int height = 1; height <= 17; ++height)
        for (int width = 1; width <= 19; ++width)
        {
            std::vector<unsigned char> input(width * height);
            for (int i = 0; i < width * height; ++i)
            {
                state = state * 1664525U + 1013904223U;
                input[i] = (state >> 27) & 1 ? 255 : 0;
            }
            for (int radius : {0, 1, 3})
                for (bool erode : {false, true})
                {
                    std::vector<unsigned char> output;
                    FaceSegmentation::Morph(input, output, width, height,
                                             radius, erode);
                    if (output != SlowMorph(input, width, height, radius, erode))
                    {
                        std::fprintf(stderr, "morph mismatch: %dx%d r=%d erode=%d\n",
                                     width, height, radius, erode);
                        return 1;
                    }
                }
        }
    std::puts("Separable morphology equivalence: PASS");
    return 0;
}
