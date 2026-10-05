#include "brothumb/metadata.h"
#include "brothumb/uri.h"
#include "check.h"

#include <chrono>
#include <fstream>
#include <thread>

int main() {
    auto temp_dir = std::filesystem::temp_directory_path();
    auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto test_src = temp_dir / ("brothumb_meta_src_" + nonce + ".txt");
    auto test_png = temp_dir / ("brothumb_meta_thumb_" + nonce + ".png");

    // Clean up helper
    struct Cleaner {
        std::filesystem::path a, b;
        ~Cleaner() {
            std::error_code ec;
            std::filesystem::remove(a, ec);
            std::filesystem::remove(b, ec);
        }
    } cleaner{test_src, test_png};

    // Create source file
    {
        std::ofstream f(test_src);
        f << "Hello Freedesktop Thumbnail Specification!\n";
    }

    // Create test image
    brothumb::Image img;
    img.width = 64;
    img.height = 64;
    img.rgba.resize(64 * 64 * 4, 128);

    // Prepare metadata
    brothumb::ThumbnailMetadata meta;
    meta.uri = brothumb::path_to_uri(test_src);
    meta.mtime = 1700000000;
    meta.file_size = 42;
    meta.mimetype = "text/plain";
    meta.image_width = 800;
    meta.image_height = 600;
    meta.software = "brothumb test suite";
    meta.custom_tags["X-Custom-Tag"] = "BroThumbValue";
    meta.custom_tags["X-Unicode-Tag"] = "caf\xC3\xA9 \xE2\x9C\x93"; // UTF-8: written as iTXt

    // Write PNG with metadata
    brothumb::Result write_res = brothumb::write_png_with_metadata(test_png, img, meta);
    CHECK(write_res.ok);
    CHECK(std::filesystem::exists(test_png));

    // Read metadata back
    brothumb::PngInfo read_info;
    brothumb::Result read_res = brothumb::read_png_metadata(test_png, read_info);
    CHECK(read_res.ok);
    CHECK_EQ(read_info.width, 64);
    CHECK_EQ(read_info.height, 64);
    CHECK_EQ(read_info.metadata.uri, meta.uri);
    CHECK_EQ(read_info.metadata.mtime, meta.mtime);
    CHECK_EQ(read_info.metadata.file_size, meta.file_size);
    CHECK_EQ(read_info.metadata.mimetype, meta.mimetype);
    CHECK_EQ(read_info.metadata.image_width, meta.image_width);
    CHECK_EQ(read_info.metadata.image_height, meta.image_height);
    CHECK_EQ(read_info.metadata.software, meta.software);
    CHECK_EQ(read_info.metadata.custom_tags["X-Custom-Tag"], "BroThumbValue");
    CHECK_EQ(read_info.metadata.custom_tags["X-Unicode-Tag"], "caf\xC3\xA9 \xE2\x9C\x93");

    // Freshness verification test
    // 1. Fresh thumbnail matching current source file
    brothumb::ThumbnailMetadata current_meta;
    current_meta.uri = brothumb::path_to_uri(test_src);
    // Overwrite PNG with current metadata
    brothumb::Result res2 = brothumb::write_png_with_metadata(test_png, img, current_meta);
    CHECK(res2.ok);

    // Re-write PNG properly using cache freshness check
    brothumb::PngInfo fresh_info;
    brothumb::read_png_metadata(test_png, fresh_info);

    // Stale check
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::ofstream f(test_src, std::ios::app);
        f << "Modified line!\n";
    }
    // Now source mtime differs
    brothumb::CacheFreshness freshness = brothumb::verify_cache_freshness(test_png, test_src);
    CHECK(freshness == brothumb::CacheFreshness::Stale || freshness == brothumb::CacheFreshness::MissingMetadata);

    return bttest::finish("test_metadata");
}
