#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include "JawCut.h"

struct Image
{
    int width = 0, height = 0;
    std::vector<unsigned char> pixels;
};

static bool ReadPnm(const char* path, const char* magic, int channels, Image& image)
{
    std::ifstream file(path, std::ios::binary);
    std::string actual;
    int maximum = 0;
    file >> actual >> image.width >> image.height >> maximum;
    file.get();
    if (!file || actual != magic || maximum != 255 ||
        image.width <= 0 || image.height <= 0)
        return false;
    image.pixels.resize(image.width * image.height * channels);
    file.read(reinterpret_cast<char*>(image.pixels.data()), image.pixels.size());
    return static_cast<bool>(file);
}

int main(int argc, char** argv)
{
    if (argc != 5)
        return 2;
    Image rgb, skin, expected;
    if (!ReadPnm(argv[1], "P6", 3, rgb) ||
        !ReadPnm(argv[2], "P5", 1, skin) ||
        !ReadPnm(argv[4], "P5", 1, expected) ||
        rgb.width != skin.width || rgb.height != skin.height ||
        rgb.width != expected.width || rgb.height != expected.height)
        return 2;
    JawCut::Curve curve;
    if (!JawCut::FindCurve(rgb.pixels.data(), skin.pixels.data(),
                           rgb.width, rgb.height, curve))
        return 1;
    std::ifstream csv(argv[3]);
    std::string header;
    std::getline(csv, header);
    double maxError = 0.0;
    int referenceX = 0;
    char comma = 0;
    double referenceY = 0.0;
    for (int x = curve.left; x <= curve.right; ++x)
    {
        csv >> referenceX >> comma >> referenceY;
        if (!csv || referenceX != x || comma != ',')
            return 1;
        maxError = std::max(maxError,
                            std::abs(curve.rows[x - curve.left] - referenceY));
    }
    int differingPixels = 0;
    for (int y = 0; y < rgb.height; ++y)
        for (int x = 0; x < rgb.width; ++x)
        {
            const bool retained = skin.pixels[y * rgb.width + x] &&
                x >= curve.left && x <= curve.right &&
                y <= curve.rows[x - curve.left];
            differingPixels += (retained ? 255 : 0) !=
                expected.pixels[y * rgb.width + x];
        }

    // 空白和贴边输入不能产生越界访问或沿用上一幅图的曲线。
    std::vector<unsigned char> blank(80 * 80, 0);
    std::vector<unsigned char> flat(80 * 80 * 3, 128);
    JawCut::Curve failed = curve;
    if (JawCut::FindCurve(flat.data(), blank.data(), 80, 80, failed) ||
        failed.left != curve.left || failed.rows != curve.rows)
        return 1;
    for (int y = 5; y < 75; ++y)
        for (int x = 0; x < 42; ++x)
            blank[y * 80 + x] = 255;
    if (JawCut::FindCurve(flat.data(), blank.data(), 80, 80, failed))
        return 1;

    std::printf("max_curve_error=%.6f differing_mask_pixels=%d\n",
                maxError, differingPixels);
    return maxError <= 0.001 && differingPixels == 0 ? 0 : 1;
}
