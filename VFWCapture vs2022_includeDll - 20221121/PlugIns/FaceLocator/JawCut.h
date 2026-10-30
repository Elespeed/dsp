#ifndef JAW_CUT_H
#define JAW_CUT_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace JawCut
{
struct Curve
{
    int left = 0;
    int right = -1;
    int mouthY = -1;
    double mouthScore = 0.0;
    double mouthSlope = 0.0;
    std::vector<double> rows;
};

template <typename T>
inline T Clamp(T value, T low, T high)
{
    return std::max(low, std::min(value, high));
}

inline std::vector<double> BlurRegion(const std::vector<double>& input,
                                      int width, int height, double sigma,
                                      bool vertical, int firstX, int lastX)
{
    const int radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
    std::vector<double> kernel(2 * radius + 1);
    double total = 0.0;
    for (int offset = -radius; offset <= radius; ++offset)
    {
        const double value = std::exp(-offset * offset / (2.0 * sigma * sigma));
        kernel[offset + radius] = value;
        total += value;
    }
    for (std::size_t i = 0; i < kernel.size(); ++i)
        kernel[i] /= total;

    std::vector<double> output(input.size(), 0.0);
    for (int y = 0; y < height; ++y)
        for (int x = std::max(0, firstX); x <= std::min(width - 1, lastX); ++x)
            for (int offset = -radius; offset <= radius; ++offset)
            {
                const int sampleX = vertical ? x : Clamp(x + offset, 0, width - 1);
                const int sampleY = vertical ? Clamp(y + offset, 0, height - 1) : y;
                output[y * width + x] +=
                    input[sampleY * width + sampleX] * kernel[offset + radius];
            }
    return output;
}

inline std::vector<double> Blur(const std::vector<double>& input,
                                int width, int height, double sigma,
                                bool vertical)
{
    return BlurRegion(input, width, height, sigma, vertical, 0, width - 1);
}

// 从 RGB 图和最大肤色连通域寻找裁剪线；失败时不修改输出。
inline bool FindCurve(const unsigned char* rgb, const unsigned char* skin,
                      int width, int height, Curve& output,
                      double mouthGapRatio = 0.16, double tiltGain = 0.35)
{
    if (!rgb || !skin || width <= 0 || height <= 0 ||
        width > std::numeric_limits<int>::max() / height ||
        mouthGapRatio < 0.08 || mouthGapRatio > 0.35 ||
        tiltGain < 0.0 || tiltGain > 1.0)
        return false;

    int left = width, right = -1, top = height, bottom = -1;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (skin[y * width + x])
            {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    if (right - left < 30 || bottom - top < 30)
        return false;

    const double faceWidth = right - left + 1.0;
    const double centerX = (left + right) / 2.0;
    // 嘴部评分只读取中央列，先收窄计算区间以减少每帧耗时。
    const int searchLeft = Clamp(static_cast<int>(std::round(centerX - 0.18 * faceWidth)),
                                 0, width - 1);
    const int searchRight = Clamp(static_cast<int>(std::round(centerX + 0.18 * faceWidth)),
                                  0, width - 1);
    const int searchTop = Clamp(static_cast<int>(top + 0.60 * faceWidth), 0, height - 1);
    const int searchBottom = Clamp(
        std::min(static_cast<int>(top + 1.25 * faceWidth),
                 static_cast<int>(bottom - 0.16 * faceWidth)),
        searchTop + 1, height);
    if (searchLeft > searchRight || searchTop >= searchBottom)
        return false;

    const int count = width * height;
    std::vector<double> gray(count), redChroma(count);
    for (int i = 0; i < count; ++i)
    {
        const double red = rgb[3 * i];
        const double green = rgb[3 * i + 1];
        const double blue = rgb[3 * i + 2];
        gray[i] = 0.299 * red + 0.587 * green + 0.114 * blue;
        redChroma[i] = 128.0 + 0.713 * (red - gray[i]);
    }
    const std::vector<double> fine = BlurRegion(gray, width, height,
        std::max(1.0, faceWidth * 0.008), true, searchLeft, searchRight);
    const std::vector<double> broad = BlurRegion(gray, width, height,
        std::max(2.0, faceWidth * 0.045), true, searchLeft, searchRight);
    const std::vector<double> redBroad = BlurRegion(redChroma, width, height,
        std::max(3.0, faceWidth * 0.09), true, searchLeft, searchRight);
    std::vector<double> feature(count);
    for (int y = 0; y < height; ++y)
        for (int x = searchLeft; x <= searchRight; ++x)
        {
            const int i = y * width + x;
            feature[i] = broad[i] - fine[i] +
                1.2 * (redChroma[i] - redBroad[i]);
        }

    double best = -std::numeric_limits<double>::infinity();
    int mouth = searchTop;
    for (int y = searchTop; y < searchBottom; ++y)
    {
        double score = 0.0;
        for (int x = searchLeft; x <= searchRight; ++x)
            score += feature[y * width + x];
        if (score > best)
        {
            best = score;
            mouth = y;
        }
    }
    const int searchColumns = searchRight - searchLeft + 1;
    const double mouthScore = best / searchColumns;
    if (mouthScore < 3.0)
        return false;

    double slope = 0.0, bestSlopeScore = -std::numeric_limits<double>::infinity();
    int mouthShift = 0;
    for (int angle = -15; angle <= 15; ++angle)
        for (int shift = -static_cast<int>(0.025 * faceWidth);
             shift <= static_cast<int>(0.025 * faceWidth); ++shift)
        {
            double score = 0.0;
            for (int x = searchLeft; x <= searchRight; ++x)
            {
                const int y = Clamp(static_cast<int>(std::round(
                    mouth + shift + 0.02 * angle * (x - centerX))), 0, height - 1);
                score += feature[y * width + x];
            }
            score = score / searchColumns - 0.25 * std::abs(shift);
            if (score > bestSlopeScore)
            {
                bestSlopeScore = score;
                slope = 0.02 * angle;
                mouthShift = shift;
            }
        }
    mouth += mouthShift;
    // 搜索带边缘的肤色轮廓容易被误识别为嘴部。
    const int interiorMargin = std::max(3, static_cast<int>(std::round(0.06 * faceWidth)));
    if (mouth <= searchTop + interiorMargin ||
        mouth >= searchBottom - 1 - interiorMargin)
        return false;

    const double edgeSigma = std::max(1.0, faceWidth * 0.01);
    const int edgeRadius = std::max(1, static_cast<int>(std::ceil(3.0 * edgeSigma)));
    gray = BlurRegion(BlurRegion(gray, width, height, edgeSigma, true,
                      left - edgeRadius, right + edgeRadius),
                      width, height, edgeSigma, false, left, right);
    const int radius = std::max(3, static_cast<int>(std::round(0.065 * faceWidth)));
    const int states = 2 * radius + 1;
    const int columns = right - left + 1;
    const int step = std::max(1, static_cast<int>(std::round(0.02 * faceWidth)));
    if (step * 2 >= height)
        return false;
    std::vector<int> back(columns * states, 0);
    std::vector<double> previous(states, 1e18), current(states), prior(columns);
    for (int x = left; x <= right; ++x)
    {
        const int column = x - left;
        const double normalized = (x - centerX) / (0.5 * faceWidth);
        prior[column] = mouth + mouthGapRatio * faceWidth +
            tiltGain * slope * (x - centerX) +
            0.015 * faceWidth * (1.0 - normalized * normalized);
        for (int state = 0; state < states; ++state)
        {
            const int offset = state - radius;
            const int y = Clamp(static_cast<int>(std::round(prior[column] + offset)),
                                step, height - 1 - step);
            const double edge = Clamp(
                (gray[(y - step) * width + x] - gray[(y + step) * width + x]) / 12.0,
                -2.0, 2.0);
            const double scaledOffset = offset / (0.028 * faceWidth);
            const double cost = scaledOffset * scaledOffset - 0.75 * edge;
            if (column == 0)
            {
                current[state] = cost;
                continue;
            }
            double minimum = 1e18;
            int predecessor = 0;
            for (int candidate = std::max(0, state - 2);
                 candidate <= std::min(states - 1, state + 2); ++candidate)
            {
                const int distance = state - candidate;
                const double value = previous[candidate] + 0.45 * distance * distance;
                if (value < minimum)
                {
                    minimum = value;
                    predecessor = candidate;
                }
            }
            current[state] = cost + minimum;
            back[column * states + state] = predecessor;
        }
        previous.swap(current);
    }

    std::vector<double> curve(columns), original(columns);
    int state = static_cast<int>(std::min_element(previous.begin(), previous.end()) -
                                 previous.begin());
    for (int column = columns - 1; column >= 0; --column)
    {
        curve[column] = prior[column] + state - radius;
        if (column)
            state = back[column * states + state];
    }
    original = curve;
    const int smooth = std::max(1, static_cast<int>(std::round(faceWidth * 0.025)));
    for (int column = 0; column < columns; ++column)
    {
        double sum = 0.0;
        int samples = 0;
        for (int neighbor = std::max(0, column - smooth);
             neighbor <= std::min(columns - 1, column + smooth); ++neighbor)
        {
            sum += original[neighbor];
            ++samples;
        }
        curve[column] = Clamp(sum / samples,
                              prior[column] - radius, prior[column] + radius);
    }
    Curve result;
    result.left = left;
    result.right = right;
    result.mouthY = mouth;
    result.mouthScore = mouthScore;
    result.mouthSlope = slope;
    result.rows.swap(curve);
    output = result;
    return true;
}
}

#endif
