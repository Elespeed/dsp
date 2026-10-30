#ifndef FACE_SEGMENTATION_H
#define FACE_SEGMENTATION_H

#include <algorithm>
#include <vector>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "JawCut.h"

namespace FaceSegmentation
{
struct Result
{
    std::vector<unsigned char> mask;
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    int pixelCount = 0;
    int neckCutRow = -1;
    int neckFaceLeft = 0;
    int neckFaceRight = 0;
    int neckCurveRise = 0;
};

inline void Morph(const std::vector<unsigned char>& input,
                  std::vector<unsigned char>& output,
                  int width, int height, int radius, bool erode)
{
    // 方形结构元素拆成水平、垂直两次扫描，边界仍按黑色像素处理。
    std::vector<unsigned char> horizontal(input.size(), erode ? 255 : 0);
    output.assign(input.size(), erode ? 255 : 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            bool hit = erode;
            for (int dx = -radius; dx <= radius; ++dx)
            {
                const int xx = x + dx;
                const bool value = xx >= 0 && xx < width &&
                    input[y * width + xx] != 0;
                if (hit != value)
                {
                    hit = value;
                    break;
                }
            }
            horizontal[y * width + x] = hit ? 255 : 0;
        }

    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            bool hit = erode;
            for (int dy = -radius; dy <= radius; ++dy)
            {
                const int yy = y + dy;
                const bool value = yy >= 0 && yy < height &&
                    horizontal[yy * width + x] != 0;
                if (hit != value)
                {
                    hit = value;
                    break;
                }
            }
            output[y * width + x] = hit ? 255 : 0;
        }
}

inline int FindRoot(std::vector<int>& parent, int label)
{
    while (parent[label] != label)
    {
        parent[label] = parent[parent[label]];
        label = parent[label];
    }
    return label;
}

inline Result RunBaseline(const unsigned char* u, const unsigned char* v,
                          int width, int height)
{
    Result result;
    if (!u || !v || width <= 0 || height <= 0)
        return result;
    const int count = width * height;
    result.mask.resize(count);
    for (int i = 0; i < count; ++i)
        result.mask[i] = u[i] >= 85 && u[i] <= 126 &&
                         v[i] >= 130 && v[i] <= 165 ? 255 : 0;

    std::vector<unsigned char> temporary;
    Morph(result.mask, temporary, width, height, 1, true);
    Morph(temporary, result.mask, width, height, 1, false);
    Morph(result.mask, temporary, width, height, 3, false);
    Morph(temporary, result.mask, width, height, 3, true);

    std::vector<int> labels(count, 0);
    std::vector<int> parent(count + 1, 0);
    int nextLabel = 1;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const int index = y * width + x;
            if (!result.mask[index])
                continue;
            const int left = x ? labels[index - 1] : 0;
            const int top = y ? labels[index - width] : 0;
            if (!left && !top)
            {
                labels[index] = nextLabel;
                parent[nextLabel] = nextLabel;
                ++nextLabel;
            }
            else if (!left || !top)
                labels[index] = left ? left : top;
            else
            {
                const int a = FindRoot(parent, left);
                const int b = FindRoot(parent, top);
                labels[index] = std::min(a, b);
                parent[std::max(a, b)] = std::min(a, b);
            }
        }

    struct Component
    {
        int count = 0;
        int minX = 0;
        int minY = 0;
        int maxX = 0;
        int maxY = 0;
    };
    std::vector<Component> components(nextLabel);
    int largest = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const int index = y * width + x;
            if (!labels[index])
                continue;
            const int root = FindRoot(parent, labels[index]);
            labels[index] = root;
            Component& c = components[root];
            if (c.count == 0)
            {
                c.minX = c.maxX = x;
                c.minY = c.maxY = y;
            }
            else
            {
                c.minX = std::min(c.minX, x);
                c.minY = std::min(c.minY, y);
                c.maxX = std::max(c.maxX, x);
                c.maxY = std::max(c.maxY, y);
            }
            ++c.count;
            if (!largest || c.count > components[largest].count)
                largest = root;
        }
    if (!largest)
        return result;

    for (int i = 0; i < count; ++i)
        if (labels[i] != largest)
            result.mask[i] = 0;
    const Component& face = components[largest];
    result.left = face.minX * 2;
    result.top = face.minY;
    result.width = (face.maxX - face.minX + 1) * 2;
    result.height = face.maxY - face.minY + 1;
    result.pixelCount = face.count * 2;
    return result;
}

inline Result RefineHeadRegion(Result baseline, int width, int height)
{
    if (baseline.pixelCount == 0 || width <= 0 || height <= 0 ||
        baseline.mask.size() != static_cast<std::vector<unsigned char>::size_type>(width * height))
        return baseline;

    const int firstRow = baseline.top;
    const int lastRow = baseline.top + baseline.height - 1;
    const int regionHeight = baseline.height;
    if (regionHeight < 20)
        return baseline;

    std::vector<int> rowWidths(height, 0);
    for (int y = firstRow; y <= lastRow; ++y)
        for (int x = 0; x < width; ++x)
            if (baseline.mask[y * width + x])
                ++rowWidths[y];

    const int upperEnd = firstRow + regionHeight / 2;
    int headWidth = 0;
    for (int y = firstRow; y < upperEnd; ++y)
        headWidth = std::max(headWidth, rowWidths[y]);
    if (headWidth < 12)
        return baseline;

    const int searchStart = firstRow + regionHeight / 4;
    const int searchEnd = firstRow + regionHeight * 4 / 5;
    int cutRow = -1;
    int neckRow = -1;
    for (int y = searchStart; y + 2 <= searchEnd; ++y)
    {
        if (rowWidths[y] * 100 > headWidth * 65 ||
            rowWidths[y + 1] * 100 > headWidth * 65 ||
            rowWidths[y + 2] * 100 > headWidth * 65)
            continue;

        int reboundRow = -1;
        for (int lower = y + 3; lower <= lastRow; ++lower)
            if (rowWidths[lower] * 100 >= headWidth * 120)
            {
                reboundRow = lower;
                break;
            }
        if (reboundRow >= 0)
        {
            neckRow = y;
            cutRow = rowWidths[y] * 100 <= headWidth * 45 ? y : reboundRow;
            break;
        }
    }
    if (cutRow < 0)
        return baseline;

    int headLeft = width, headRight = -1;
    for (int y = firstRow; y < neckRow; ++y)
    {
        if (rowWidths[y] * 100 < headWidth * 75)
            continue;
        for (int x = 0; x < width; ++x)
            if (baseline.mask[y * width + x])
            {
                headLeft = std::min(headLeft, x);
                headRight = std::max(headRight, x);
            }
    }
    if (headRight < headLeft)
    {
        headLeft = 0;
        headRight = width - 1;
    }
    else
    {
        const int margin = std::max(1, headWidth / 20);
        headLeft = std::max(0, headLeft - margin);
        headRight = std::min(width - 1, headRight + margin);
    }

    int minX = width, maxX = -1, minY = height, maxY = -1, pixels = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            unsigned char& value = baseline.mask[y * width + x];
            if (y >= cutRow || x < headLeft || x > headRight)
                value = 0;
            if (!value)
                continue;
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
            ++pixels;
        }
    if (!pixels)
        return baseline;
    baseline.left = minX * 2;
    baseline.top = minY;
    baseline.width = (maxX - minX + 1) * 2;
    baseline.height = maxY - minY + 1;
    baseline.pixelCount = pixels * 2;
    return baseline;
}

inline Result RunRefined(const unsigned char* u, const unsigned char* v,
                         int width, int height)
{
    return RefineHeadRegion(RunBaseline(u, v, width, height), width, height);
}

// 根据原图亮度估计下巴位置，并清除其下方的肤色区域。
inline Result RefineNeckRegion(Result current, int width, int height,
                               const unsigned char* luma, int lumaStride)
{
    if (current.pixelCount == 0 || width <= 0 || height <= 0 ||
        !luma || lumaStride < width * 2 ||
        current.mask.size() != static_cast<std::vector<unsigned char>::size_type>(width * height))
        return current;

    const int first = current.top;
    const int last = current.top + current.height - 1;
    if (last - first < 20)
        return current;

    // 先找肤色块最宽的一行，再在下方寻找与背景分开的面部横向区间。
    int widestRow = first, widestCount = 0;
    for (int y = first; y <= last; ++y)
    {
        int count = 0;
        for (int x = 0; x < width; ++x)
            count += current.mask[y * width + x] != 0;
        if (count > widestCount)
        {
            widestCount = count;
            widestRow = y;
        }
    }

    int faceLeft = current.left / 2;
    int faceRight = faceLeft + current.width / 2 - 1;
    int bestWidth = 0, neighborLeft = -1, neighborRight = -1;
    for (int y = widestRow + 3; y <= last - 4; ++y)
    {
        struct Run { int left, right; };
        std::vector<Run> runs;
        for (int x = 0; x < width; )
        {
            if (!current.mask[y * width + x]) { ++x; continue; }
            const int start = x;
            while (x < width && current.mask[y * width + x]) ++x;
            if (x - start >= 5)
                runs.push_back({start, x - 1});
        }
        if (runs.size() < 2)
            continue;
        for (std::size_t i = 0; i < runs.size(); ++i)
        {
            const int runWidth = runs[i].right - runs[i].left + 1;
            if (runWidth <= bestWidth)
                continue;
            bestWidth = runWidth;
            faceLeft = runs[i].left;
            faceRight = runs[i].right;
            neighborLeft = i ? runs[i - 1].right : -1;
            neighborRight = i + 1 < runs.size() ? runs[i + 1].left : -1;
        }
    }

    if (bestWidth)
    {
        const int margin = std::max(2, bestWidth / 4);
        faceLeft = std::max(0, faceLeft - margin);
        faceRight = std::min(width - 1, faceRight + margin);
        if (neighborLeft >= 0)
            faceLeft = std::max(faceLeft, neighborLeft + 1);
        if (neighborRight >= 0)
            faceRight = std::min(faceRight, neighborRight - 1);
    }

    std::vector<int> rowWidths(height, 0);
    int headWidth = bestWidth;
    for (int y = first; y <= last; ++y)
    {
        for (int x = faceLeft; x <= faceRight; ++x)
            rowWidths[y] += current.mask[y * width + x] != 0;
        if (!bestWidth && rowWidths[y] > headWidth)
            headWidth = rowWidths[y];
    }
    if (headWidth < 12)
        return current;

    int cutRow = last + 1;
    {
        // 在脸的中央窄带寻找嘴部的亮度谷值，再按上半脸距离估计下巴。
        std::vector<int> smoothWidths = rowWidths;
        for (int y = first + 1; y < last; ++y)
            smoothWidths[y] = (rowWidths[y - 1] + 2 * rowWidths[y] +
                               rowWidths[y + 1] + 2) / 4;
        const int peakWidth = *std::max_element(smoothWidths.begin() + first,
                                                smoothWidths.begin() + last + 1);
        int faceTop = -1;
        for (int y = first; y + 2 <= last; ++y)
            if (smoothWidths[y] * 2 >= peakWidth &&
                smoothWidths[y + 1] * 2 >= peakWidth &&
                smoothWidths[y + 2] * 2 >= peakWidth)
            {
                faceTop = y;
                break;
            }
        if (faceTop >= 0)
        {
            const int low = faceTop + (peakWidth * 2 + 2) / 5;
            const int high = std::min(last, faceTop + (peakWidth * 9 + 5) / 10);
            const int center = (faceLeft + faceRight) / 2;
            const int span = std::max(2, (faceRight - faceLeft) / 6);
            std::vector<int> brightness;
            int mouthRow = -1, mouthValue = 256;
            for (int y = low; y <= high; ++y)
            {
                int sum = 0;
                const int sampleLeft = std::max(0, center - span);
                const int sampleRight = std::min(width - 1, center + span);
                for (int x = sampleLeft; x <= sampleRight; ++x)
                    sum += luma[y * lumaStride + x * 2];
                const int value = sum / (sampleRight - sampleLeft + 1);
                brightness.push_back(value);
                if (value < mouthValue)
                {
                    mouthValue = value;
                    mouthRow = y;
                }
            }
            if (!brightness.empty())
            {
                std::sort(brightness.begin(), brightness.end());
                const int median = brightness[brightness.size() / 2];
                if (median - mouthValue >= 12)
                {
                    cutRow = std::min(last + 1,
                        mouthRow + (3 * (mouthRow - faceTop) + 2) / 5 + 3);
                }
            }
        }
    }
    if (!bestWidth && cutRow == last + 1)
        return current;

    if (cutRow <= last)
    {
        // 嘴部亮度给出下巴初值；若近处肤色行宽突然收窄，则把切线移到收窄处下方。
        const int searchFirst = std::max(first + 1, cutRow - 10);
        const int searchLast = std::min(last, cutRow - 3);
        const int earliestCut = cutRow - 6;
        int adjustedCut = cutRow;
        for (int y = searchFirst; y <= searchLast; ++y)
        {
            const int upperWidth = rowWidths[y - 1];
            const int widthDrop = upperWidth - rowWidths[y];
            if (upperWidth >= 20 && widthDrop >= 6 &&
                widthDrop * 5 >= upperWidth)
                adjustedCut = std::max(earliestCut, y + 3);
        }
        cutRow = adjustedCut;
    }

    Result candidate = current;
    const int center = (faceLeft + faceRight) / 2;
    const int halfWidth = std::max(1, (faceRight - faceLeft) / 2);
    const int curveRise = cutRow <= last ? std::max(2, headWidth / 6) : 0;
    int minX = width, maxX = -1, minY = height, maxY = -1, pixels = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            unsigned char& value = candidate.mask[y * width + x];
            const int dx = x - center;
            const int lift = curveRise * dx * dx / (halfWidth * halfWidth);
            if (y >= cutRow - lift || x < faceLeft || x > faceRight)
                value = 0;
            if (!value)
                continue;
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
            ++pixels;
        }
    if (!pixels || pixels * 2 < current.pixelCount / 3)
        return current;
    candidate.left = minX * 2;
    candidate.top = minY;
    candidate.width = (maxX - minX + 1) * 2;
    candidate.height = maxY - minY + 1;
    candidate.pixelCount = pixels * 2;
    if (cutRow <= last)
    {
        candidate.neckCutRow = cutRow;
        candidate.neckFaceLeft = faceLeft;
        candidate.neckFaceRight = faceRight;
        candidate.neckCurveRise = curveRise;
    }
    return candidate;
}

inline Result RunNeckRefined(const unsigned char* u, const unsigned char* v,
                             int width, int height,
                             const unsigned char* luma, int lumaStride)
{
    return RefineNeckRegion(RunRefined(u, v, width, height), width, height,
                            luma, lumaStride);
}

// 课程基线之后只运行新的嘴部引导裁剪；弱证据时保留本帧基线。
inline Result RunJawRefined(const unsigned char* u, const unsigned char* v,
                            int width, int height,
                            const unsigned char* originalYuv,
                            JawCut::Curve* diagnostic = 0)
{
    if (diagnostic)
        *diagnostic = JawCut::Curve();
    Result baseline = RunBaseline(u, v, width, height);
    if (!baseline.pixelCount || !originalYuv || width <= 0 || height <= 0 ||
        width > 44739242 / height)
        return baseline;

    const int originalWidth = width * 4;
    const int originalHeight = height * 4;
    const int originalPixels = originalWidth * originalHeight;
    std::vector<unsigned char> rgb(originalPixels * 3);
    std::vector<unsigned char> fullMask(originalPixels);
    const unsigned char* originalY = originalYuv;
    const unsigned char* originalU = originalY + originalPixels;
    const unsigned char* originalV = originalU + originalPixels / 2;
    for (int y = 0; y < originalHeight; ++y)
        for (int x = 0; x < originalWidth; ++x)
        {
            const int index = y * originalWidth + x;
            const int chroma = y * (originalWidth / 2) + x / 2;
            const double luma = originalY[index];
            const double cb = originalU[chroma] - 128;
            const double cr = originalV[chroma] - 128;
            rgb[index * 3] = static_cast<unsigned char>(JawCut::Clamp(
                static_cast<int>(luma + 1.370705 * cr), 0, 255));
            rgb[index * 3 + 1] = static_cast<unsigned char>(JawCut::Clamp(
                static_cast<int>(luma - 0.698001 * cr - 0.337633 * cb), 0, 255));
            rgb[index * 3 + 2] = static_cast<unsigned char>(JawCut::Clamp(
                static_cast<int>(luma + 1.732446 * cb), 0, 255));
            fullMask[index] = baseline.mask[(y / 4) * width + x / 4];
        }

    JawCut::Curve curve;
    if (!JawCut::FindCurve(rgb.data(), fullMask.data(), originalWidth,
                           originalHeight, curve))
        return baseline;

    Result result = baseline;
    int minX = width, minY = height, maxX = -1, maxY = -1, pixels = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            unsigned char& value = result.mask[y * width + x];
            if (!value)
                continue;
            const int originalX = x * 4 + 2;
            const int originalYRow = y * 4 + 2;
            if (originalX < curve.left || originalX > curve.right ||
                originalYRow > curve.rows[originalX - curve.left])
            {
                value = 0;
                continue;
            }
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
            ++pixels;
        }
    if (!pixels)
        return baseline;
    result.left = minX * 2;
    result.top = minY;
    result.width = (maxX - minX + 1) * 2;
    result.height = maxY - minY + 1;
    result.pixelCount = pixels * 2;
    if (diagnostic)
        *diagnostic = curve;
    return result;
}
}

#endif
