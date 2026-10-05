// Freedesktop XDG Thumbnail Cache implementation.
#pragma once

#include "brothumb/common.h"
#include "brothumb/metadata.h"
#include <filesystem>
#include <optional>
#include <string>

namespace brothumb {

// Result of a cache lookup.
struct CacheLookupResult {
    bool found = false;
    bool is_failed = false; // Source has a valid failure record in fail/ directory
    std::filesystem::path thumbnail_path;
    ThumbnailSize found_size = ThumbnailSize::Normal;
    ThumbnailMetadata metadata;
    Image image; // Populated if image was loaded
};

// Manages the XDG thumbnail cache hierarchy (normal, large, x-large, xx-large, fail).
class ThumbnailCache {
public:
    // If base_dir is empty, defaults to $XDG_CACHE_HOME/thumbnails,
    // ~/.cache/thumbnails, or platform equivalent.
    explicit ThumbnailCache(std::filesystem::path base_dir = std::filesystem::path());

    // Returns the root directory of this thumbnail cache.
    const std::filesystem::path& base_dir() const { return base_dir_; }

    // Returns the directory for a given thumbnail size (e.g. base_dir / "normal").
    std::filesystem::path directory_for_size(ThumbnailSize size) const;

    // Returns the directory for failed thumbnails (base_dir / "fail" or base_dir / "fail" / app_name).
    std::filesystem::path directory_for_fail(const std::string& app_name = "") const;

    // Computes the expected thumbnail path for a source file at the given size.
    std::filesystem::path thumbnail_path_for_source(const std::filesystem::path& source_path,
                                                    ThumbnailSize size) const;

    // Checks the cache for a thumbnail matching the source path.
    // If load_image is true, decodes the cached PNG into the result.
    // If allow_larger is true, will fall back to larger sizes if requested size is absent.
    CacheLookupResult lookup(const std::filesystem::path& source_path, ThumbnailSize size,
                             bool load_image = true, bool allow_larger = true);

    // Stores an image into the cache atomically (temporary file + rename + 0600 permissions).
    Result store(const std::filesystem::path& source_path, ThumbnailSize size,
                 const Image& image, const ThumbnailMetadata& extra_meta = ThumbnailMetadata());

    // Records a failure for a source file in the fail/ directory so subsequent
    // attempts don't repeatedly try and fail.
    Result record_failure(const std::filesystem::path& source_path,
                          const std::string& app_name = "");

    // Checks whether a failure record exists and is fresh (matches current source mtime).
    bool has_failed(const std::filesystem::path& source_path,
                    const std::string& app_name = "") const;

    // Clears any failure record for the source path.
    Result clear_failure(const std::filesystem::path& source_path,
                         const std::string& app_name = "");

    // Deletes cached thumbnails for a given source path across all size tiers.
    Result invalidate(const std::filesystem::path& source_path);

    // Determines the default platform XDG cache directory.
    static std::filesystem::path default_cache_dir();

private:
    std::filesystem::path base_dir_;
};

}  // namespace brothumb
