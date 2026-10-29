#include <algorithm>
#include <cstdio>
#include <vector>
#include "FaceSegmentation.h"

static void Fill(std::vector<unsigned char>& u, std::vector<unsigned char>& v,
                 int width, int left, int top, int right, int bottom)
{
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
        {
            u[y * width + x] = 100;
            v[y * width + x] = 145;
        }
}

int main()
{
    const int width = 160, height = 120;
    std::vector<unsigned char> u(width * height, 0), v(width * height, 0);
    std::vector<unsigned char> luma(width * 2 * height, 150);

    // 浅窄颈下方连着宽肩；旧算法只裁肩，新算法应继续裁掉颈部。
    Fill(u, v, width, 63, 18, 97, 57);
    Fill(u, v, width, 71, 57, 88, 76);
    Fill(u, v, width, 42, 76, 118, 102);
    std::fill(luma.begin() + 42 * width * 2,
              luma.begin() + 43 * width * 2, 90);
    const auto oldResult = FaceSegmentation::RunRefined(u.data(), v.data(), width, height);
    const auto neckResult = FaceSegmentation::RunNeckRefined(
        u.data(), v.data(), width, height, luma.data(), width * 2);
    if (!oldResult.mask[68 * width + 80] || neckResult.mask[68 * width + 80] ||
        !neckResult.mask[45 * width + 80] || neckResult.height >= oldResult.height)
    {
        std::puts("浅窄颈裁剪失败");
        return 1;
    }

    // 只有短下巴、没有颈部尾巴时，保留原本的全部面部像素。
    std::fill(u.begin(), u.end(), 0);
    std::fill(v.begin(), v.end(), 0);
    std::fill(luma.begin(), luma.end(), 150);
    Fill(u, v, width, 63, 18, 97, 57);
    for (int y = 57; y < 63; ++y)
        Fill(u, v, width, 65 + (y - 57), y, 95 - (y - 57), y + 1);
    std::fill(luma.begin() + 44 * width * 2,
              luma.begin() + 45 * width * 2, 90);
    const auto chin = FaceSegmentation::RunRefined(u.data(), v.data(), width, height);
    const auto keptChin = FaceSegmentation::RefineNeckRegion(
        chin, width, height, luma.data(), width * 2);
    if (keptChin.mask != chin.mask)
    {
        std::puts("无颈样本误切下巴");
        return 1;
    }

    // 与面部相连的细长背景不应撑大最终包围框。
    std::fill(u.begin(), u.end(), 0);
    std::fill(v.begin(), v.end(), 0);
    std::fill(luma.begin(), luma.end(), 150);
    Fill(u, v, width, 50, 30, 82, 82);
    Fill(u, v, width, 59, 82, 73, 97);
    Fill(u, v, width, 100, 15, 112, 106);
    Fill(u, v, width, 80, 55, 101, 65);
    std::fill(luma.begin() + 65 * width * 2,
              luma.begin() + 66 * width * 2, 90);
    const auto connected = FaceSegmentation::RunBaseline(u.data(), v.data(), width, height);
    const auto separated = FaceSegmentation::RefineNeckRegion(
        connected, width, height, luma.data(), width * 2);
    if (!connected.mask[75 * width + 105] || separated.mask[75 * width + 105] ||
        !separated.mask[55 * width + 65] || separated.mask[90 * width + 65])
    {
        std::puts("背景与颈部连通样本裁剪失败");
        return 1;
    }
    std::puts("Face neck refinement: PASS");
    return 0;
}
