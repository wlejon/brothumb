#include "brothumb/cache.h"
#include "brothumb/uri.h"
#include "check.h"

#include <chrono>
#include <fstream>

int main() {
    auto temp_dir = std::filesystem::temp_directory_path();
    auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto private_cache_dir = temp_dir / ("brothumb_test_cache_" + nonce);
    auto test_src1 = temp_dir / ("brothumb_cache_src1_" + nonce + ".txt");
    auto test_src2 = temp_dir / ("brothumb_cache_src2_" + nonce + ".txt");

    // RAII Cleaner
    struct Cleaner {
        std::filesystem::path dir, f1, f2;
        ~Cleaner() {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
            std::filesystem::remove(f1, ec);
            std::filesystem::remove(f2, ec);
        }
    } cleaner{private_cache_dir, test_src1, test_src2};

    // Create source files
    {
        std::ofstream f(test_src1);
        f << "Test file 1 content for cache tests\n";
    }
    {
        std::ofstream f(test_src2);
        f << "Test file 2 content for cache fallback tests\n";
    }

    brothumb::ThumbnailCache cache(private_cache_dir);
    CHECK_EQ(cache.base_dir(), private_cache_dir);

    // 1. Check directory structure
    auto normal_dir = cache.directory_for_size(brothumb::ThumbnailSize::Normal);
    CHECK_EQ(normal_dir, private_cache_dir / "normal");
    CHECK(std::filesystem::exists(normal_dir));

    auto large_dir = cache.directory_for_size(brothumb::ThumbnailSize::Large);
    CHECK_EQ(large_dir, private_cache_dir / "large");
    CHECK(std::filesystem::exists(large_dir));

    auto fail_dir = cache.directory_for_fail();
    CHECK_EQ(fail_dir, private_cache_dir / "fail");
    CHECK(std::filesystem::exists(fail_dir));

    // 2. Test Store & Exact Lookup
    brothumb::Image img1;
    img1.width = 128;
    img1.height = 128;
    img1.rgba.resize(128 * 128 * 4, 200);

    brothumb::Result store_res = cache.store(test_src1, brothumb::ThumbnailSize::Normal, img1);
    CHECK(store_res.ok);

    auto expected_path1 = cache.thumbnail_path_for_source(test_src1, brothumb::ThumbnailSize::Normal);
    CHECK(std::filesystem::exists(expected_path1));

    auto lookup1 = cache.lookup(test_src1, brothumb::ThumbnailSize::Normal, true, false);
    CHECK(lookup1.found);
    CHECK_EQ(lookup1.found_size, brothumb::ThumbnailSize::Normal);
    CHECK_EQ(lookup1.image.width, 128);
    CHECK_EQ(lookup1.image.height, 128);

    // 3. Test Fallback Lookup (Large -> Normal)
    brothumb::Image img2;
    img2.width = 256;
    img2.height = 256;
    img2.rgba.resize(256 * 256 * 4, 150);

    brothumb::Result store_large = cache.store(test_src2, brothumb::ThumbnailSize::Large, img2);
    CHECK(store_large.ok);

    // Request Normal (128x128), but only Large (256x256) exists in cache
    auto lookup_fallback = cache.lookup(test_src2, brothumb::ThumbnailSize::Normal, true, true);
    CHECK(lookup_fallback.found);
    CHECK_EQ(lookup_fallback.found_size, brothumb::ThumbnailSize::Large);
    CHECK_EQ(lookup_fallback.image.width, 128);
    CHECK_EQ(lookup_fallback.image.height, 128);

    // 4. Test Failure Recording
    CHECK(!cache.has_failed(test_src1));
    brothumb::Result fail_res = cache.record_failure(test_src1, "test_app");
    CHECK(fail_res.ok);
    CHECK(cache.has_failed(test_src1));

    auto lookup_failed = cache.lookup(test_src1, brothumb::ThumbnailSize::Normal);
    CHECK(lookup_failed.is_failed);

    cache.clear_failure(test_src1, "test_app");
    CHECK(!cache.has_failed(test_src1));

    // 5. Test Invalidation
    cache.invalidate(test_src1);
    auto lookup_after_inv = cache.lookup(test_src1, brothumb::ThumbnailSize::Normal);
    CHECK(!lookup_after_inv.found);

    return bttest::finish("test_cache");
}
