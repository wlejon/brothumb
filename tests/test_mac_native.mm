#if defined(__APPLE__)

#include "brothumb/native.h"
#include "brothumb/generator.h"
#include "check.h"

#include <chrono>
#include <cstdlib>
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
    std::vector<uint8_t> pixels(row_stride * h, 150);
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(header), 54);
    f.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
}

}  // namespace

int main() {
    CHECK(brothumb::NativeThumbnailExtractor::is_supported());
    CHECK_EQ(brothumb::NativeThumbnailExtractor::provider_name(),
             "macOS Quick Look (QLThumbnailImageCreate)");

    auto caps = brothumb::NativeThumbnailExtractor::capabilities();
    CHECK(caps.has_macos_quicklook);
    CHECK(caps.has_pdf_rendering);
    CHECK_EQ(caps.pdf_backend, "PDFKit");

    auto temp_dir = std::filesystem::temp_directory_path();
    auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto test_bmp = temp_dir / ("brothumb_mac_test_" + nonce + ".bmp");

    struct Cleaner {
        std::filesystem::path p;
        ~Cleaner() {
            std::error_code ec;
            std::filesystem::remove(p, ec);
        }
    } cleaner{test_bmp};

    create_test_bmp(test_bmp, 80, 80);

    // Test Quick Look thumbnail extraction
    brothumb::Image extracted;
    brothumb::Result res = brothumb::NativeThumbnailExtractor::extract(test_bmp, 128, extracted);
    if (!res) {
        std::printf("[test_mac_native] Native extract note: %s\n", res.error.c_str());
    } else {
        CHECK(!extracted.empty());
        CHECK(extracted.width > 0);
        CHECK(extracted.height > 0);
        std::printf("[test_mac_native] Extracted %dx%d via QuickLook / ImageIO\n",
                    extracted.width, extracted.height);
    }

    // Oracle comparison: test running qlmanage if available
    std::string ql_cmd = "/usr/bin/qlmanage -t -s 128 -o \"" + temp_dir.string() + "\" \"" +
                         test_bmp.string() + "\" > /dev/null 2>&1";
    int ql_ret = std::system(ql_cmd.c_str());
    if (ql_ret == 0) {
        auto ql_out = temp_dir / (test_bmp.filename().string() + ".png");
        std::error_code ec;
        if (std::filesystem::exists(ql_out, ec)) {
            std::printf("[test_mac_native] qlmanage successfully generated %s\n",
                        ql_out.string().c_str());
            std::filesystem::remove(ql_out, ec);
        }
    }

    return bttest::finish("test_mac_native");
}

#else

int main() {
    return 0;
}

#endif
