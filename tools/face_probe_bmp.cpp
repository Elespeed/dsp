#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <climits>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "FaceSegmentation.h"

namespace fs = std::filesystem;

struct BmpImage
{
    std::vector<unsigned char> bytes;
    int width = 0, height = 0, storedHeight = 0;
    std::uint64_t stride = 0, offset = 0;
};

struct ProbeResult
{
    FaceSegmentation::Result baseline, refined;
    long long baselineUs = 0, refineUs = 0;
};

static std::uint16_t Read16(const std::vector<unsigned char>& b, std::size_t i)
{
    return static_cast<std::uint16_t>(b[i] | (b[i + 1] << 8));
}

static std::uint32_t Read32(const std::vector<unsigned char>& b, std::size_t i)
{
    return static_cast<std::uint32_t>(b[i]) |
           (static_cast<std::uint32_t>(b[i + 1]) << 8) |
           (static_cast<std::uint32_t>(b[i + 2]) << 16) |
           (static_cast<std::uint32_t>(b[i + 3]) << 24);
}

static bool ReadBmp(const fs::path& path, BmpImage& image)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    image.bytes.assign(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
    const auto& b = image.bytes;
    if (b.size() < 54 || b[0] != 'B' || b[1] != 'M' ||
        Read32(b, 14) < 40 || Read16(b, 26) != 1 ||
        Read16(b, 28) != 24 || Read32(b, 30) != 0)
        return false;
    const std::int32_t width = static_cast<std::int32_t>(Read32(b, 18));
    const std::int32_t storedHeight = static_cast<std::int32_t>(Read32(b, 22));
    if (width <= 0 || width % 16 != 0 || storedHeight == 0 ||
        storedHeight == INT32_MIN)
        return false;
    const std::int32_t height = storedHeight < 0 ? -storedHeight : storedHeight;
    if (height % 4 != 0)
        return false;
    const std::uint64_t stride = (static_cast<std::uint64_t>(width) * 3 + 3) & ~3ULL;
    const std::uint64_t offset = Read32(b, 10);
    if (offset + stride * height > b.size())
        return false;
    image.width = width;
    image.height = height;
    image.storedHeight = storedHeight;
    image.stride = stride;
    image.offset = offset;
    return true;
}

static ProbeResult Probe(const BmpImage& image)
{
    const int mw = image.width / 4, mh = image.height / 4;
    std::vector<unsigned char> u(mw * mh), v(mw * mh);
    for (int y = 0; y < mh; ++y)
        for (int x = 0; x < mw; ++x)
        {
            const int sourceY = y * 4;
            const int bmpY = image.storedHeight > 0 ? image.height - 1 - sourceY : sourceY;
            const std::size_t i = static_cast<std::size_t>(image.offset +
                image.stride * bmpY + static_cast<std::uint64_t>(x * 4) * 3);
            const int b = image.bytes[i], g = image.bytes[i + 1], r = image.bytes[i + 2];
            u[y * mw + x] = static_cast<unsigned char>(
                static_cast<int>(-0.1687 * r - 0.3313 * g + 0.5 * b + 128));
            v[y * mw + x] = static_cast<unsigned char>(
                static_cast<int>(0.5 * r - 0.4187 * g - 0.0813 * b + 128));
        }
    ProbeResult result;
    const auto start = std::chrono::steady_clock::now();
    result.baseline = FaceSegmentation::RunBaseline(u.data(), v.data(), mw, mh);
    const auto middle = std::chrono::steady_clock::now();
    result.refined = FaceSegmentation::RefineHeadRegion(result.baseline, mw, mh);
    const auto end = std::chrono::steady_clock::now();
    result.baselineUs = std::chrono::duration_cast<std::chrono::microseconds>(
        middle - start).count();
    result.refineUs = std::chrono::duration_cast<std::chrono::microseconds>(
        end - middle).count();
    return result;
}

static bool WriteMask(const fs::path& path, const FaceSegmentation::Result& face,
                      int width, int height)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out << "P5\n" << width << ' ' << height << "\n255\n";
    out.write(reinterpret_cast<const char*>(face.mask.data()),
              static_cast<std::streamsize>(face.mask.size()));
    return static_cast<bool>(out);
}

static bool WriteOverlay(const fs::path& path, const BmpImage& image,
                         const FaceSegmentation::Result& face, bool refined)
{
    std::vector<unsigned char> out = image.bytes;
    if (face.pixelCount)
    {
        // 降采样彩色图坐标换算到原图；基线框为青色，精修框为紫色。
        const int left = face.left * 2, top = face.top * 4;
        const int right = left + face.width * 2 - 1;
        const int bottom = top + face.height * 4 - 1;
        for (int y = std::max(0, top); y <= std::min(image.height - 1, bottom); ++y)
            for (int x = std::max(0, left); x <= std::min(image.width - 1, right); ++x)
            {
                if (x > left + 1 && x < right - 1 &&
                    y > top + 1 && y < bottom - 1)
                    continue;
                const int bmpY = image.storedHeight > 0 ? image.height - 1 - y : y;
                const std::size_t i = static_cast<std::size_t>(image.offset +
                    image.stride * bmpY + static_cast<std::uint64_t>(x) * 3);
                out[i] = 255;
                out[i + 1] = refined ? 0 : 255;
                out[i + 2] = refined ? 255 : 0;
            }
    }
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return false;
    file.write(reinterpret_cast<const char*>(out.data()),
               static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(file);
}

static bool WriteResults(const fs::path& prefix, const BmpImage& image,
                         const ProbeResult& result)
{
    const int mw = image.width / 4, mh = image.height / 4;
    return WriteMask(prefix.string() + "_baseline.pgm", result.baseline, mw, mh) &&
           WriteMask(prefix.string() + "_refined.pgm", result.refined, mw, mh) &&
           WriteOverlay(prefix.string() + "_baseline.bmp", image, result.baseline, false) &&
           WriteOverlay(prefix.string() + "_refined.bmp", image, result.refined, true);
}

static void WriteCsvHeader(std::ostream& out)
{
    out << "file,width,height,baseline_left,baseline_top,baseline_width,"
           "baseline_height,baseline_pixels,baseline_us,refined_left,"
           "refined_top,refined_width,refined_height,refined_pixels,refine_us\n";
}

static void WriteCsvRow(std::ostream& out, const std::string& name,
                        const BmpImage& image, const ProbeResult& result)
{
    const auto& b = result.baseline;
    const auto& r = result.refined;
    out << '"' << name << "\"," << image.width << ',' << image.height << ','
        << b.left << ',' << b.top << ',' << b.width << ',' << b.height << ','
        << b.pixelCount << ',' << result.baselineUs << ','
        << r.left << ',' << r.top << ',' << r.width << ',' << r.height << ','
        << r.pixelCount << ',' << result.refineUs << '\n';
}

int main(int argc, char** argv)
{
    const bool batch = argc == 4 && std::string(argv[1]) == "--batch";
    if (!(argc == 3 || batch))
    {
        std::fprintf(stderr, "usage: face_probe_bmp input.bmp output_prefix\n"
                             "   or: face_probe_bmp --batch input_dir output_dir\n");
        return 2;
    }
    if (!batch)
    {
        BmpImage image;
        if (!ReadBmp(argv[1], image))
        {
            std::fprintf(stderr, "expected uncompressed 24-bit BMP with width divisible by 16 and height divisible by 4\n");
            return 2;
        }
        const ProbeResult result = Probe(image);
        if (!WriteResults(argv[2], image, result))
            return 2;
        std::ofstream csv(std::string(argv[2]) + ".csv");
        WriteCsvHeader(csv);
        WriteCsvRow(csv, fs::path(argv[1]).filename().string(), image, result);
        std::printf("baseline: %d %d %d %d pixels=%d time=%lldus\n",
            result.baseline.left, result.baseline.top, result.baseline.width,
            result.baseline.height, result.baseline.pixelCount, result.baselineUs);
        std::printf("refined:  %d %d %d %d pixels=%d time=%lldus\n",
            result.refined.left, result.refined.top, result.refined.width,
            result.refined.height, result.refined.pixelCount, result.refineUs);
        return csv ? 0 : 2;
    }
    const fs::path inputDir(argv[2]), outputDir(argv[3]);
    if (!fs::is_directory(inputDir))
        return 2;
    fs::create_directories(outputDir);
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(inputDir))
        if (entry.is_regular_file())
        {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".bmp")
                files.push_back(entry.path());
        }
    std::sort(files.begin(), files.end());
    std::ofstream csv(outputDir / "results.csv");
    if (!csv || files.empty())
        return 2;
    WriteCsvHeader(csv);
    int failures = 0;
    for (const fs::path& path : files)
    {
        BmpImage image;
        if (!ReadBmp(path, image))
        {
            std::fprintf(stderr, "skip invalid BMP: %s\n", path.string().c_str());
            ++failures;
            continue;
        }
        const ProbeResult result = Probe(image);
        if (!WriteResults(outputDir / path.stem(), image, result))
        {
            ++failures;
            continue;
        }
        WriteCsvRow(csv, path.filename().string(), image, result);
        if (!csv)
            return 2;
    }
    std::printf("processed %zu BMP files, failures=%d\n", files.size(), failures);
    return failures ? 1 : 0;
}
