#if !defined(_WIN32) && !defined(__APPLE__)

#include "brothumb/cache.h"
#include "brothumb/generator.h"
#include "brothumb/metadata.h"
#include "brothumb/native.h"
#include "brothumb/uri.h"
#include "check.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

int main() {
    auto caps = brothumb::NativeThumbnailExtractor::capabilities();
    CHECK(caps.has_xdg_cache);
    std::printf("[test_linux_native] Provider: %s (PDF backend: %s)\n",
                brothumb::NativeThumbnailExtractor::provider_name().c_str(),
                caps.pdf_backend.c_str());

    auto temp_dir = std::filesystem::temp_directory_path();
    auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto private_cache = temp_dir / ("brothumb_linux_cache_" + nonce);
    auto test_src = temp_dir / ("brothumb_linux_src_" + nonce + ".txt");

    struct Cleaner {
        std::filesystem::path dir, src;
        ~Cleaner() {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
            std::filesystem::remove(src, ec);
        }
    } cleaner{private_cache, test_src};

    {
        std::ofstream f(test_src);
        f << "Linux Freedesktop XDG Thumbnail standard test file.\n";
    }

    // Store a thumbnail into private cache
    brothumb::ThumbnailCache cache(private_cache);
    brothumb::Image img;
    img.width = 128;
    img.height = 128;
    img.rgba.resize(128 * 128 * 4, 180);

    brothumb::Result res = cache.store(test_src, brothumb::ThumbnailSize::Normal, img);
    CHECK(res.ok);

    auto thumb_path = cache.thumbnail_path_for_source(test_src, brothumb::ThumbnailSize::Normal);
    CHECK(std::filesystem::exists(thumb_path));

    // Verify Freedesktop permission requirement: 0600 (owner read/write only)
    struct stat st;
    int stat_ret = ::stat(thumb_path.c_str(), &st);
    CHECK_EQ(stat_ret, 0);
    mode_t mode = st.st_mode & 0777;
    CHECK_EQ(mode, static_cast<mode_t>(S_IRUSR | S_IWUSR));

    // Oracle verification: check with system file utility
    std::string file_cmd = "file \"" + thumb_path.string() + "\" 2>&1";
    FILE* fp = popen(file_cmd.c_str(), "r");
    if (fp) {
        char buf[256];
        std::string output;
        while (fgets(buf, sizeof(buf), fp)) {
            output += buf;
        }
        pclose(fp);
        CHECK(output.find("PNG image data") != std::string::npos);
        std::printf("[test_linux_native] Oracle 'file' output: %s\n", output.c_str());
    }

    // Verify metadata tags via read_png_metadata
    brothumb::PngInfo info;
    brothumb::Result meta_res = brothumb::read_png_metadata(thumb_path, info);
    CHECK(meta_res.ok);
    CHECK_EQ(info.metadata.uri, brothumb::path_to_uri(test_src));
    CHECK(info.metadata.mtime > 0);

    // Verify lookup finds it
    auto lookup = cache.lookup(test_src, brothumb::ThumbnailSize::Normal);
    CHECK(lookup.found);
    CHECK(!lookup.image.empty());

    return bttest::finish("test_linux_native");
}

#else

int main() {
    return 0;
}

#endif
