#include <cstdint>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "FaceSegmentation.h"

static std::uint16_t Read16(const std::vector<unsigned char>& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

static std::uint32_t Read32(const std::vector<unsigned char>& bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

static bool WriteMask(const std::string& path, const FaceSegmentation::Result& result,
                      int width, int height)
{
    std::ofstream output(path.c_str(), std::ios::binary);
    if (!output)
        return false;
    output << "P5\n" << width << ' ' << height << "\n255\n";
    output.write(reinterpret_cast<const char*>(result.mask.data()),
                 static_cast<std::streamsize>(result.mask.size()));
    return static_cast<bool>(output);
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: face_probe_bmp input.bmp output_prefix\n");
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input)
        return 2;
    const std::vector<unsigned char> bmp((std::istreambuf_iterator<char>(input)),
                                          std::istreambuf_iterator<char>());
    if (bmp.size() < 54 || bmp[0] != 'B' || bmp[1] != 'M' ||
        Read32(bmp, 14) < 40 || Read16(bmp, 26) != 1 ||
        Read16(bmp, 28) != 24 || Read32(bmp, 30) != 0)
    {
        std::fprintf(stderr, "only uncompressed 24-bit BMP is supported\n");
        return 2;
    }
    const std::int32_t width = static_cast<std::int32_t>(Read32(bmp, 18));
    const std::int32_t storedHeight = static_cast<std::int32_t>(Read32(bmp, 22));
    if (width <= 0 || width % 4 != 0 || storedHeight == 0 ||
        storedHeight == INT32_MIN)
        return 2;
    const std::int32_t height = storedHeight < 0 ? -storedHeight : storedHeight;
    if (height % 4 != 0)
        return 2;
    const std::uint64_t stride = (static_cast<std::uint64_t>(width) * 3 + 3) & ~3ULL;
    const std::uint64_t offset = Read32(bmp, 10);
    if (offset + stride * height > bmp.size())
        return 2;

    const int maskWidth = width / 4, maskHeight = height / 4;
    std::vector<unsigned char> u(maskWidth * maskHeight);
    std::vector<unsigned char> v(maskWidth * maskHeight);
    for (int y = 0; y < maskHeight; ++y)
        for (int x = 0; x < maskWidth; ++x)
        {
            const int sourceY = y * 4;
            const int bmpY = storedHeight > 0 ? height - 1 - sourceY : sourceY;
            const std::size_t pixel = static_cast<std::size_t>(offset +
                stride * bmpY + static_cast<std::uint64_t>(x * 4) * 3);
            const int b = bmp[pixel], g = bmp[pixel + 1], r = bmp[pixel + 2];
            const int uu = static_cast<int>(-0.1687 * r - 0.3313 * g + 0.5 * b + 128);
            const int vv = static_cast<int>(0.5 * r - 0.4187 * g - 0.0813 * b + 128);
            u[y * maskWidth + x] = static_cast<unsigned char>(uu);
            v[y * maskWidth + x] = static_cast<unsigned char>(vv);
        }

    const FaceSegmentation::Result baseline =
        FaceSegmentation::RunBaseline(u.data(), v.data(), maskWidth, maskHeight);
    const FaceSegmentation::Result refined =
        FaceSegmentation::RunRefined(u.data(), v.data(), maskWidth, maskHeight);
    std::printf("baseline: %d %d %d %d pixels=%d\n", baseline.left,
        baseline.top, baseline.width, baseline.height, baseline.pixelCount);
    std::printf("refined:  %d %d %d %d pixels=%d\n", refined.left,
        refined.top, refined.width, refined.height, refined.pixelCount);
    const std::string prefix(argv[2]);
    if (!WriteMask(prefix + "_baseline.pgm", baseline, maskWidth, maskHeight) ||
        !WriteMask(prefix + "_refined.pgm", refined, maskWidth, maskHeight))
        return 2;
    return 0;
}
