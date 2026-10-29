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

// 去脖子实验函数；实拍样本仍存在误切下巴，插件暂不调用。
inline Result RefineNeckRegion(Result current, int width, int height)
{
    if (current.pixelCount == 0 || width <= 0 || height <= 0 ||
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
    int bestWidth = 0, bestRow = first, neighborLeft = -1, neighborRight = -1;
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
            bestRow = y;
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
    int headWidth = bestWidth, headPeak = bestRow;
    for (int y = first; y <= last; ++y)
    {
        for (int x = faceLeft; x <= faceRight; ++x)
            rowWidths[y] += current.mask[y * width + x] != 0;
        if (!bestWidth && rowWidths[y] > headWidth)
        {
            headWidth = rowWidths[y];
            headPeak = y;
        }
    }
    if (headWidth < 12)
        return current;

    // 只裁掉持续变窄的尾部；短暂变窄可能仍是下巴，需保留。
    int cutRow = last + 1;
    const int searchStart = headPeak + std::max(8, headWidth / 3);
    for (int y = searchStart; y + 4 <= last; ++y)
    {
        if (rowWidths[y] * 100 > headWidth * 55 ||
            rowWidths[y + 1] * 100 > headWidth * 55 ||
            rowWidths[y + 2] * 100 > headWidth * 55)
            continue;
        cutRow = y;
        break;
    }
    if (!bestWidth && cutRow == last + 1)
        return current;

    Result candidate = current;
    int minX = width, maxX = -1, minY = height, maxY = -1, pixels = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            unsigned char& value = candidate.mask[y * width + x];
            if (y >= cutRow || x < faceLeft || x > faceRight)
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
    return candidate;
}

inline Result RunNeckRefined(const unsigned char* u, const unsigned char* v,
                             int width, int height)
{
    return RefineNeckRegion(RunRefined(u, v, width, height), width, height);
}
}

#endif
