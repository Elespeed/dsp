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
    output.assign(input.size(), erode ? 255 : 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            bool hit = erode;
            for (int dy = -radius; dy <= radius; ++dy)
            {
                const int yy = y + dy;
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    const int xx = x + dx;
                    const bool value = yy >= 0 && yy < height &&
                        xx >= 0 && xx < width &&
                        input[yy * width + xx] != 0;
                    if (erode && !value)
                    {
                        hit = false;
                        break;
                    }
                    if (!erode && value)
                    {
                        hit = true;
                        break;
                    }
                }
                if (hit != erode)
                    break;
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
    for (int y = searchStart; y + 2 <= searchEnd; ++y)
    {
        if (rowWidths[y] * 100 > headWidth * 65 ||
            rowWidths[y + 1] * 100 > headWidth * 65 ||
            rowWidths[y + 2] * 100 > headWidth * 65)
            continue;

        int lowerWidth = 0;
        for (int lower = y + 3; lower <= lastRow; ++lower)
            lowerWidth = std::max(lowerWidth, rowWidths[lower]);
        if (lowerWidth * 100 >= headWidth * 120)
        {
            cutRow = y;
            break;
        }
    }
    if (cutRow < 0)
        return baseline;

    int minX = width, maxX = -1, minY = height, maxY = -1, pixels = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            unsigned char& value = baseline.mask[y * width + x];
            if (y >= cutRow)
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
}

#endif
