// High-level ThumbnailService providing async requests and host event queue.
#pragma once

#include "brothumb/cache.h"
#include "brothumb/common.h"
#include "brothumb/event_queue.h"
#include "brothumb/generator.h"
#include <filesystem>
#include <memory>
#include <string>
#include <variant>

namespace brothumb {

// Events emitted onto the MessageQueue<ThumbnailEvent> for the host to drain.
struct ThumbnailReady {
    RequestId request_id = 0;
    std::filesystem::path path;
    ThumbnailSize size = ThumbnailSize::Normal;
    ThumbnailSource source = ThumbnailSource::Unknown;
    Image image;
};

struct ThumbnailFailed {
    RequestId request_id = 0;
    std::filesystem::path path;
    ThumbnailSize size = ThumbnailSize::Normal;
    std::string error;
};

struct ThumbnailCanceled {
    RequestId request_id = 0;
    std::filesystem::path path;
    ThumbnailSize size = ThumbnailSize::Normal;
};

using ThumbnailEvent = std::variant<ThumbnailReady, ThumbnailFailed, ThumbnailCanceled>;

class ThumbnailService {
public:
    struct Config {
        // Background worker threads; 0 sizes the pool from the hardware
        // (default_thread_count()).
        size_t thread_count = 0;
        std::filesystem::path cache_dir;       // Custom cache directory (empty = default XDG)
        bool enable_native = true;             // Allow OS native extraction (Shell/QuickLook)
        bool record_failures = true;           // Write to fail/ cache on repeated errors
    };

    static std::unique_ptr<ThumbnailService> create(const Config& config,
                                                    std::string* err = nullptr);
    static std::unique_ptr<ThumbnailService> create();

    // The pool a Config with thread_count 0 gets: every hardware thread but
    // two (one on a machine with three or fewer), which are left to the UI
    // that asked for the thumbnails. A camera card's photos decode in parallel
    // this way: each costs tens of milliseconds of one core however small the
    // thumbnail, since a JPEG's entropy-coded data is read serially.
    static size_t default_thread_count();

    virtual ~ThumbnailService() = default;

    // Asynchronously submits a request to generate or fetch a thumbnail.
    // Returns a unique RequestId. Result will be pushed to events().
    virtual RequestId request(std::filesystem::path path, ThumbnailOptions options = ThumbnailOptions()) = 0;

    // Cancels a pending or active request by ID.
    virtual void cancel(RequestId request_id) = 0;

    // Synchronous request: looks up cache or generates immediately on the calling thread.
    virtual Result get_sync(const std::filesystem::path& path, const ThumbnailOptions& options,
                            Image& out_image, ThumbnailSource* out_source = nullptr) = 0;

    // Access the thread-safe message queue. Host drains this on its own thread.
    virtual MessageQueue<ThumbnailEvent>& events() = 0;

    // Reports platform capabilities honestly.
    virtual PlatformCapabilities capabilities() const = 0;

    // Access to underlying XDG cache manager.
    virtual ThumbnailCache& cache() = 0;
    virtual const ThumbnailCache& cache() const = 0;
};

}  // namespace brothumb
