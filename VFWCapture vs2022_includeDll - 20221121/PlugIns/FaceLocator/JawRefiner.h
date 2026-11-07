#ifndef DSP_JAW_REFINER_H
#define DSP_JAW_REFINER_H

// 纯 C++11 下颚与颈部精修算法，不依赖 OpenCV 或训练模型。
// 输入为 width×height 的肤色 mask，以及宽度为 2×width 的降采样 Y 平面。
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace JawRefiner
{
struct Curve
{
    bool valid;
    int left;
    int right;
    double centerX;
    double headWidth;
    double peakRow;
    double chinRow;
    std::vector<double> rows;

    Curve()
        : valid(false), left(0), right(-1), centerX(0.0), headWidth(0.0),
          peakRow(0.0), chinRow(0.0)
    {
    }
};

struct Result
{
    std::vector<unsigned char> mask;
    int left;
    int top;
    int width;
    int height;
    int pixelCount;
    bool valid;
    double centerX;
    double headWidth;
    double peakRow;
    double chinRow;

    Result()
        : left(0), top(0), width(0), height(0), pixelCount(0), valid(false),
          centerX(0.0), headWidth(0.0), peakRow(0.0), chinRow(0.0)
    {
    }
};

struct Options
{
    bool edgeRefine;
    double chinFactor;
    double sideFactor;
    double halfWidthFactor;
    double edgeWindowFactor;

    Options()
        : edgeRefine(false), chinFactor(0.63), sideFactor(0.16),
          halfWidthFactor(0.43), edgeWindowFactor(0.085)
    {
    }
};

inline int ClampInt(int value, int low, int high)
{
    return std::max(low, std::min(value, high));
}

inline double ClampDouble(double value, double low, double high)
{
    return std::max(low, std::min(value, high));
}

inline int Round(double value)
{
    return static_cast<int>(std::floor(value + 0.5));
}

inline double Median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t count = values.size();
    return count % 2 ? values[count / 2]
                     : (values[count / 2 - 1] + values[count / 2]) * 0.5;
}

inline double Prior(double x, double centerX, double side, double chin,
                    double halfWidth)
{
    const double ratio = ClampDouble(
        std::fabs(x - centerX) / std::max(1.0, halfWidth), 0.0, 1.0);
    return side + (chin - side) * (1.0 - std::pow(ratio, 1.65));
}

inline Result Apply(const std::vector<unsigned char>& original,
                    const unsigned char* grayY, int grayStride,
                    int width, int height, const Options& options = Options(),
                    Curve* diagnostic = 0)
{
    Result output;
    if (diagnostic)
        *diagnostic = Curve();
    if (width < 20 || height < 20 || grayStride < 2 * width || !grayY ||
        original.size() != static_cast<std::size_t>(width) * height)
        return output;

    int top = height;
    int bottom = -1;
    std::vector<int> rowWidth(height, 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (original[y * width + x])
            {
                ++rowWidth[y];
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    if (bottom - top + 1 < 20)
        return output;

    const int rawHeight = bottom - top + 1;
    const int upperEnd = std::min(
        bottom + 1, top + std::max(20, Round(rawHeight * 0.55)));
    std::vector<double> smoothWidth(height, 0.0);
    for (int y = top; y < upperEnd; ++y)
    {
        std::vector<int> neighboring;
        for (int offset = -2; offset <= 2; ++offset)
            neighboring.push_back(
                rowWidth[ClampInt(y + offset, 0, height - 1)]);
        std::sort(neighboring.begin(), neighboring.end());
        smoothWidth[y] = neighboring[2];
    }

    // 选择最宽且稳定的 13 行脸部带，避免脸颊与衣服瞬时连通造成宽度突增。
    int peak = top;
    double bestMean = -1.0;
    for (int center = top; center < upperEnd; ++center)
    {
        double sum = 0.0;
        for (int offset = -6; offset <= 6; ++offset)
            sum += smoothWidth[ClampInt(center + offset, top, upperEnd - 1)];
        if (sum > bestMean)
        {
            bestMean = sum;
            peak = center;
        }
    }

    std::vector<double> localWidths;
    for (int y = std::max(top, peak - 6);
         y <= std::min(upperEnd - 1, peak + 6); ++y)
        localWidths.push_back(smoothWidth[y]);
    const double faceWidth = Median(localWidths);
    if (faceWidth < 12.0 || faceWidth > width * 0.90)
        return output;

    std::vector<double> centers;
    for (int y = std::max(top, peak - 5);
         y <= std::min(bottom, peak + 5); ++y)
    {
        int left = width;
        int right = -1;
        int count = 0;
        for (int x = 0; x < width; ++x)
            if (original[y * width + x])
            {
                left = std::min(left, x);
                right = std::max(right, x);
                ++count;
            }
        if (count >= faceWidth * 0.70 && right >= left)
            centers.push_back((left + right) * 0.5);
    }
    if (centers.empty())
        return output;

    const double centerX = Median(centers);
    const double side = peak + options.sideFactor * faceWidth;
    const double chin = peak + options.chinFactor * faceWidth;
    const double halfWidth = std::max(5.0, options.halfWidthFactor * faceWidth);
    if (chin >= height - 3 || chin <= peak)
        return output;

    output.centerX = centerX;
    output.headWidth = faceWidth;
    output.peakRow = peak;
    output.chinRow = chin;

    std::vector<double> contour(width);
    for (int x = 0; x < width; ++x)
        contour[x] = Prior(x, centerX, side, chin, halfWidth);

    if (options.edgeRefine)
    {
        // 使用可分离 [1,2,1]/4 核近似高斯模糊，保留亮度边缘方向。
        std::vector<double> gray(width * height, 0.0);
        std::vector<double> temporary(width * height, 0.0);
        std::vector<double> blurred(width * height, 0.0);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                gray[y * width + x] =
                    0.5 * (grayY[y * grayStride + x * 2] +
                           grayY[y * grayStride + x * 2 + 1]);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                temporary[y * width + x] =
                    (gray[y * width + ClampInt(x - 1, 0, width - 1)] +
                     2.0 * gray[y * width + x] +
                     gray[y * width + ClampInt(x + 1, 0, width - 1)]) /
                    4.0;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                blurred[y * width + x] =
                    (temporary[ClampInt(y - 1, 0, height - 1) * width + x] +
                     2.0 * temporary[y * width + x] +
                     temporary[ClampInt(y + 1, 0, height - 1) * width + x]) /
                    4.0;

        const int start = ClampInt(Round(centerX - halfWidth), 0, width - 1);
        const int stop = ClampInt(Round(centerX + halfWidth), 0, width - 1);
        const int window = std::max(4, Round(options.edgeWindowFactor * faceWidth));
        const int stateCount = window * 2 + 1;
        const int columnCount = stop - start + 1;
        const double invalidScore = -1.e20;
        std::vector<double> scores(stateCount, invalidScore);
        std::vector<double> nextScores(stateCount, invalidScore);
        std::vector<int> positions(columnCount * stateCount, 0);
        std::vector<int> trace(columnCount * stateCount, 0);
        for (int column = 0; column < columnCount; ++column)
        {
            const int x = start + column;
            std::fill(nextScores.begin(), nextScores.end(), invalidScore);
            for (int state = 0; state < stateCount; ++state)
            {
                const int offset = state - window;
                const int y = Round(contour[x] + offset);
                positions[column * stateCount + state] = y;
                if (y < 4 || y >= height - 4)
                    continue;
                const double gradient =
                    blurred[(y - 2) * width + x] - blurred[(y + 2) * width + x];
                const double directional = ClampDouble(gradient, 0.0, 65.0);
                const double magnitude = std::min(std::fabs(gradient), 80.0);
                const double observation =
                    0.56 * directional + 0.18 * magnitude - offset * offset;
                if (column == 0)
                {
                    nextScores[state] = observation;
                    continue;
                }
                const double priorDelta = contour[x] - contour[x - 1];
                double best = invalidScore;
                int bestState = 0;
                for (int previous = 0; previous < stateCount; ++previous)
                {
                    const int deltaY =
                        y - positions[(column - 1) * stateCount + previous];
                    if (std::abs(deltaY) > 2)
                        continue;
                    const double error = deltaY - priorDelta;
                    const double value = scores[previous] - 1.1 * error * error;
                    if (value > best)
                    {
                        best = value;
                        bestState = previous;
                    }
                }
                nextScores[state] = observation + best;
                trace[column * stateCount + state] = bestState;
            }
            scores.swap(nextScores);
        }

        int bestState = 0;
        for (int state = 1; state < stateCount; ++state)
            if (scores[state] > scores[bestState])
                bestState = state;
        for (int column = columnCount - 1; column >= 0; --column)
        {
            contour[start + column] =
                positions[column * stateCount + bestState];
            bestState = trace[column * stateCount + bestState];
        }
        std::vector<double> smoothed = contour;
        for (int x = start; x <= stop; ++x)
        {
            std::vector<double> values;
            for (int offset = -2; offset <= 2; ++offset)
                values.push_back(contour[ClampInt(x + offset, start, stop)]);
            smoothed[x] = Median(values);
        }
        contour.swap(smoothed);
    }

    output.mask = original;
    const double horizontalLimit = 0.55 * faceWidth;
    int minX = width;
    int maxX = -1;
    int minY = height;
    int maxY = -1;
    int kept = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            if (!output.mask[y * width + x])
                continue;
            if (std::fabs(x - centerX) > horizontalLimit ||
                y > Round(contour[x]))
            {
                output.mask[y * width + x] = 0;
                continue;
            }
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
            ++kept;
        }
    if (!kept)
    {
        output.mask.clear();
        return output;
    }

    output.left = 2 * minX;
    output.top = minY;
    output.width = 2 * (maxX - minX + 1);
    output.height = maxY - minY + 1;
    output.pixelCount = kept * 2;
    output.valid = true;

    if (diagnostic)
    {
        diagnostic->valid = true;
        diagnostic->centerX = centerX;
        diagnostic->headWidth = faceWidth;
        diagnostic->peakRow = peak;
        diagnostic->chinRow = chin;
        diagnostic->left = ClampInt(Round(centerX - horizontalLimit), 0,
                                    width - 1);
        diagnostic->right = ClampInt(Round(centerX + horizontalLimit), 0,
                                     width - 1);
        diagnostic->rows.assign(contour.begin() + diagnostic->left,
                                contour.begin() + diagnostic->right + 1);
    }
    return output;
}
} // namespace JawRefiner

#endif
