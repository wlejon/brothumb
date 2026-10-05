// Platform native thumbnail extractors (Windows Shell / macOS Quick Look).
#pragma once

#include "brothumb/common.h"
#include <filesystem>
#include <string>

namespace brothumb {

// Provides access to platform native thumbnail services:
// - Windows: IShellItemImageFactory / IThumbnailCache (COM)
// - macOS: Quick Look (QLThumbnailImageCreate / QuickLook framework)
// - Linux: Native XDG cache and desktop thumbnailers
class NativeThumbnailExtractor {
public:
    // Returns true if the current platform provides native OS thumbnail extraction.
    static bool is_supported();

    // Human-readable name of the platform native extractor.
    static std::string provider_name();

    // Queries native capabilities of the platform honestly.
    static PlatformCapabilities capabilities();

    // Extracts a thumbnail using the OS native thumbnailing system.
    static Result extract(const std::filesystem::path& path, int32_t target_size,
                          Image& out_image);
};

}  // namespace brothumb
