#include "brothumb/cache.h"
#include "brothumb/uri.h"
#include "image_resizer.h"
#include "lodepng/lodepng.h"

#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

namespace brothumb {

namespace {

int64_t get_file_mtime(const std::filesystem::path& path) {
#if defined(_WIN32)
    struct _stat64 st;
    if (_wstat64(path.wstring().c_str(), &st) == 0) return static_cast<int64_t>(st.st_mtime);
#else
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) return static_cast<int64_t>(st.st_mtime);
#endif
    return 0;
}

int64_t get_file_size(const std::filesystem::path& path) {
#if defined(_WIN32)
    struct _stat64 st;
    if (_wstat64(path.wstring().c_str(), &st) == 0) return static_cast<int64_t>(st.st_size);
#else
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) return static_cast<int64_t>(st.st_size);
#endif
    return -1;
}

}  // namespace

std::filesystem::path ThumbnailCache::default_cache_dir() {
#if defined(_WIN32)
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data && *local_app_data) {
        return std::filesystem::path(local_app_data) / "thumbnails";
    }
    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && *user_profile) {
        return std::filesystem::path(user_profile) / ".cache" / "thumbnails";
    }
    return std::filesystem::temp_directory_path() / "thumbnails";
#else
    const char* xdg_cache = std::getenv("XDG_CACHE_HOME");
    if (xdg_cache && *xdg_cache) {
        return std::filesystem::path(xdg_cache) / "thumbnails";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::filesystem::path(home) / ".cache" / "thumbnails";
    }
    return std::filesystem::temp_directory_path() / ".cache" / "thumbnails";
#endif
}

ThumbnailCache::ThumbnailCache(std::filesystem::path base_dir)
    : base_dir_(base_dir.empty() ? default_cache_dir() : std::move(base_dir)) {
}

std::filesystem::path ThumbnailCache::directory_for_size(ThumbnailSize size) const {
    auto dir = base_dir_ / to_string(size);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path ThumbnailCache::directory_for_fail(const std::string& app_name) const {
    auto dir = app_name.empty() ? (base_dir_ / "fail") : (base_dir_ / "fail" / app_name);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path ThumbnailCache::thumbnail_path_for_source(
    const std::filesystem::path& source_path, ThumbnailSize size) const {
    std::string uri = path_to_uri(source_path);
    std::string fname = uri_to_thumbnail_filename(uri);
    return directory_for_size(size) / fname;
}

CacheLookupResult ThumbnailCache::lookup(const std::filesystem::path& source_path,
                                        ThumbnailSize size,
                                        bool load_image,
                                        bool allow_larger) {
    CacheLookupResult result;

    std::error_code ec;
    if (!std::filesystem::exists(source_path, ec)) {
        return result;
    }

    std::string uri = path_to_uri(source_path);
    std::string fname = uri_to_thumbnail_filename(uri);

    // 1. Check failure cache
    if (has_failed(source_path)) {
        result.is_failed = true;
        return result;
    }

    // 2. Helper lambda to check candidate file
    auto try_candidate = [&](ThumbnailSize s, const std::filesystem::path& path) -> bool {
        if (!std::filesystem::exists(path, ec)) return false;
        if (verify_cache_freshness(path, source_path) != CacheFreshness::Valid) return false;

        PngInfo info;
        if (!read_png_metadata(path, info)) return false;

        result.found = true;
        result.thumbnail_path = path;
        result.found_size = s;
        result.metadata = std::move(info.metadata);

        if (load_image) {
            std::vector<uint8_t> bytes;
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (f.is_open()) {
                auto sz = f.tellg();
                f.seekg(0, std::ios::beg);
                bytes.resize(static_cast<size_t>(sz));
                f.read(reinterpret_cast<char*>(bytes.data()), sz);

                unsigned w = 0, h = 0;
                Image decoded;
                if (!lodepng::decode(decoded.rgba, w, h, bytes.data(), bytes.size())) {
                    decoded.width = static_cast<int32_t>(w);
                    decoded.height = static_cast<int32_t>(h);

                    if (s != size) {
                        // Downscale larger thumbnail to requested size
                        result.image = resize_to_fit(decoded, to_pixels(size));
                    } else {
                        result.image = std::move(decoded);
                    }
                }
            }
        }
        return true;
    };

    // Try exact requested size
    auto exact_path = directory_for_size(size) / fname;
    if (try_candidate(size, exact_path)) {
        return result;
    }

    // Fall back to larger size if allowed
    if (allow_larger) {
        static const ThumbnailSize size_order[] = {
            ThumbnailSize::Normal, ThumbnailSize::Large,
            ThumbnailSize::XLarge, ThumbnailSize::XXLarge
        };

        bool passed_requested = false;
        for (ThumbnailSize s : size_order) {
            if (s == size) {
                passed_requested = true;
                continue;
            }
            if (!passed_requested) continue; // smaller than requested

            auto larger_path = directory_for_size(s) / fname;
            if (try_candidate(s, larger_path)) {
                return result;
            }
        }
    }

    return result;
}

Result ThumbnailCache::store(const std::filesystem::path& source_path,
                            ThumbnailSize size,
                            const Image& image,
                            const ThumbnailMetadata& extra_meta) {
    if (image.empty()) {
        return Result::failure("Cannot store empty thumbnail image");
    }

    std::string uri = path_to_uri(source_path);
    int64_t mtime = get_file_mtime(source_path);
    int64_t fsize = get_file_size(source_path);

    ThumbnailMetadata meta = extra_meta;
    meta.uri = uri;
    meta.mtime = mtime;
    if (meta.file_size < 0) meta.file_size = fsize;
    if (meta.software.empty()) meta.software = "brothumb 0.1.0";

    auto dest_path = thumbnail_path_for_source(source_path, size);
    Result res = write_png_with_metadata(dest_path, image, meta);
    if (res) {
        // Clear failure marker if one existed
        clear_failure(source_path);
    }
    return res;
}

Result ThumbnailCache::record_failure(const std::filesystem::path& source_path,
                                     const std::string& app_name) {
    std::string uri = path_to_uri(source_path);
    std::string fname = uri_to_thumbnail_filename(uri);
    int64_t mtime = get_file_mtime(source_path);

    ThumbnailMetadata meta;
    meta.uri = uri;
    meta.mtime = mtime;
    meta.software = app_name.empty() ? "brothumb" : app_name;

    // Per Freedesktop spec, write a 1x1 transparent dummy PNG
    Image dummy;
    dummy.width = 1;
    dummy.height = 1;
    dummy.rgba = {0, 0, 0, 0};

    auto fail_path = directory_for_fail(app_name) / fname;
    return write_png_with_metadata(fail_path, dummy, meta);
}

bool ThumbnailCache::has_failed(const std::filesystem::path& source_path,
                                const std::string& app_name) const {
    std::string uri = path_to_uri(source_path);
    std::string fname = uri_to_thumbnail_filename(uri);

    std::error_code ec;
    if (!app_name.empty()) {
        auto specific = directory_for_fail(app_name) / fname;
        if (std::filesystem::exists(specific, ec) &&
            verify_cache_freshness(specific, source_path) == CacheFreshness::Valid) {
            return true;
        }
    }

    auto fail_root = base_dir_ / "fail";
    auto root_fail = fail_root / fname;
    if (std::filesystem::exists(root_fail, ec) &&
        verify_cache_freshness(root_fail, source_path) == CacheFreshness::Valid) {
        return true;
    }

    if (std::filesystem::exists(fail_root, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(fail_root, ec)) {
            if (entry.is_directory()) {
                auto sub_path = entry.path() / fname;
                if (std::filesystem::exists(sub_path, ec) &&
                    verify_cache_freshness(sub_path, source_path) == CacheFreshness::Valid) {
                    return true;
                }
            }
        }
    }

    return false;
}

Result ThumbnailCache::clear_failure(const std::filesystem::path& source_path,
                                    const std::string& app_name) {
    std::string uri = path_to_uri(source_path);
    std::string fname = uri_to_thumbnail_filename(uri);

    std::error_code ec;
    if (!app_name.empty()) {
        auto fail_path = directory_for_fail(app_name) / fname;
        std::filesystem::remove(fail_path, ec);
    }

    auto fail_root = base_dir_ / "fail";
    std::filesystem::remove(fail_root / fname, ec);

    if (std::filesystem::exists(fail_root, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(fail_root, ec)) {
            if (entry.is_directory()) {
                std::filesystem::remove(entry.path() / fname, ec);
            }
        }
    }

    return Result::success();
}

Result ThumbnailCache::invalidate(const std::filesystem::path& source_path) {
    std::string uri = path_to_uri(source_path);
    std::string fname = uri_to_thumbnail_filename(uri);

    static const ThumbnailSize sizes[] = {
        ThumbnailSize::Normal, ThumbnailSize::Large,
        ThumbnailSize::XLarge, ThumbnailSize::XXLarge
    };

    std::error_code ec;
    for (ThumbnailSize s : sizes) {
        auto path = directory_for_size(s) / fname;
        std::filesystem::remove(path, ec);
    }

    clear_failure(source_path);
    return Result::success();
}

}  // namespace brothumb
