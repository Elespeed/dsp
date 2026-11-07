#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
#include "JawRefiner.h"

struct RawSample
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> mask;
    std::vector<unsigned char> reducedY;
};

static bool ReadSample(const char* path, RawSample& sample)
{
    std::ifstream input(path, std::ios::binary);
    std::uint32_t width = 0, height = 0;
    input.read(reinterpret_cast<char*>(&width), sizeof(width));
    input.read(reinterpret_cast<char*>(&height), sizeof(height));
    if (!input || width < 20 || height < 20 || width > 5000 || height > 5000)
        return false;

    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<unsigned char> gray(count);
    sample.mask.resize(count);
    sample.reducedY.resize(count * 2);
    input.read(reinterpret_cast<char*>(sample.mask.data()), count);
    input.read(reinterpret_cast<char*>(gray.data()), count);
    if (!input)
        return false;
    for (std::size_t i = 0; i < count; ++i)
    {
        sample.reducedY[i * 2] = gray[i];
        sample.reducedY[i * 2 + 1] = gray[i];
    }
    sample.width = static_cast<int>(width);
    sample.height = static_cast<int>(height);
    return true;
}

static bool Near(double actual, double expected)
{
    return std::fabs(actual - expected) <= 0.001;
}

static bool CheckSample(const char* path, int left, int top, int width,
                        int height, int pixels, double centerX,
                        double headWidth, double peakRow, double chinRow)
{
    RawSample sample;
    if (!ReadSample(path, sample))
        return false;
    JawRefiner::Options options;
    options.edgeRefine = false;
    JawRefiner::Curve curve;
    const JawRefiner::Result result = JawRefiner::Apply(
        sample.mask, sample.reducedY.data(), sample.width * 2,
        sample.width, sample.height, options, &curve);
    if (!result.valid || !curve.valid || curve.rows.empty() ||
        result.left != left || result.top != top || result.width != width ||
        result.height != height || result.pixelCount != pixels ||
        !Near(result.centerX, centerX) ||
        !Near(result.headWidth, headWidth) ||
        !Near(result.peakRow, peakRow) || !Near(result.chinRow, chinRow) ||
        !Near(curve.centerX, centerX) || !Near(curve.chinRow, chinRow))
    {
        std::fprintf(stderr,
            "%s: valid=%d box=%d,%d,%d,%d pixels=%d center=%.3f "
            "head=%.3f peak=%.3f chin=%.3f\n",
            path, result.valid, result.left, result.top, result.width,
            result.height, result.pixelCount, result.centerX,
            result.headWidth, result.peakRow, result.chinRow);
        return false;
    }
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    if (!CheckSample(argv[1], 152, 30, 80, 49, 2572,
                     95.5, 39.0, 53.0, 77.57) ||
        !CheckSample(argv[2], 152, 30, 76, 47, 2414,
                     95.0, 37.0, 53.0, 76.31))
        return 1;

    // 空白输入和过窄区域都不能产生残留的上一帧诊断曲线。
    std::vector<unsigned char> blank(80 * 80, 0);
    std::vector<unsigned char> gray(80 * 80 * 2, 128);
    JawRefiner::Curve curve;
    curve.valid = true;
    curve.rows.push_back(1.0);
    JawRefiner::Result failed = JawRefiner::Apply(
        blank, gray.data(), 160, 80, 80, JawRefiner::Options(), &curve);
    if (failed.valid || curve.valid || !curve.rows.empty())
        return 1;

    for (int y = 10; y < 55; ++y)
        for (int x = 35; x < 45; ++x)
            blank[y * 80 + x] = 255;
    failed = JawRefiner::Apply(
        blank, gray.data(), 160, 80, 80, JawRefiner::Options(), &curve);
    if (failed.valid || curve.valid)
        return 1;

    std::puts("JawRefiner controlled-pair regression: PASS");
    return 0;
}
