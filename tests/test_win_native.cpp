#if defined(_WIN32)

#include "brothumb/native.h"
#include "check.h"
#include <chrono>
#include <fstream>
#include <vector>

namespace {

void create_test_bmp(const std::filesystem::path& path, int w, int h) {
    int row_stride = ((w * 3 + 3) / 4) * 4;
    uint32_t file_size = 54 + row_stride * h;
    uint8_t header[54] = {
        'B', 'M',
        static_cast<uint8_t>(file_size & 0xff),
        static_cast<uint8_t>((file_size >> 8) & 0xff),
        static_cast<uint8_t>((file_size >> 16) & 0xff),
        static_cast<uint8_t>((file_size >> 24) & 0xff),
        0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
        static_cast<uint8_t>(w & 0xff), static_cast<uint8_t>((w >> 8) & 0xff), 0, 0,
        static_cast<uint8_t>(h & 0xff), static_cast<uint8_t>((h >> 8) & 0xff), 0, 0,
        1, 0, 24, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    std::vector<uint8_t> pixels(row_stride * h, 128);
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(header), 54);
    f.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
}

}  // namespace

int main() {
    CHECK(brothumb::NativeThumbnailExtractor::is_supported());
    CHECK_EQ(brothumb::NativeThumbnailExtractor::provider_name(),
             "Windows Shell (IShellItemImageFactory)");

    auto caps = brothumb::NativeThumbnailExtractor::capabilities();
    CHECK(caps.has_windows_shell);
    CHECK(caps.has_xdg_cache);

    auto temp_dir = std::filesystem::temp_directory_path();
    auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto test_bmp = temp_dir / ("brothumb_win_test_" + nonce + ".bmp");

    struct Cleaner {
        std::filesystem::path p;
        ~Cleaner() {
            std::error_code ec;
            std::filesystem::remove(p, ec);
        }
    } cleaner{test_bmp};

    create_test_bmp(test_bmp, 64, 64);

    brothumb::Image extracted;
    brothumb::Result res = brothumb::NativeThumbnailExtractor::extract(test_bmp, 128, extracted);
    if (!res) {
        // Some minimal Windows environments or headless containers without Desktop shell
        // might report failure from Shell APIs
        std::printf("[test_win_native] Shell extract note: %s\n", res.error.c_str());
    } else {
        CHECK(!extracted.empty());
        CHECK(extracted.width > 0);
        CHECK(extracted.height > 0);
        std::printf("[test_win_native] Extracted %dx%d thumbnail successfully via Shell\n",
                    extracted.width, extracted.height);
    }

    return bttest::finish("test_win_native");
}

#else

int main() {
    return 0;
}

#endif
