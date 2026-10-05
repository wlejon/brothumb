#include "brothumb/service.h"
#include "brothumb/native.h"
#include "brothumb/pool.h"
#include "image_io.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace brothumb {

namespace {

class ThumbnailServiceImpl : public ThumbnailService {
public:
    explicit ThumbnailServiceImpl(Config config)
        : config_(std::move(config)),
          cache_(config_.cache_dir),
          pool_(config_.thread_count) {
    }

    ~ThumbnailServiceImpl() override {
        pool_.stop();
    }

    RequestId request(std::filesystem::path path, ThumbnailOptions options) override {
        RequestId id = ++next_id_;
        CancellationToken token = options.token;

        {
            std::lock_guard<std::mutex> lock(active_mutex_);
            active_tokens_[id] = token;
        }

        pool_.enqueue(id, options.priority, token, [this, id, path = std::move(path), options]() {
            if (options.token.is_canceled()) {
                events_.push(ThumbnailCanceled{id, path, options.size});
                cleanup_active(id);
                return;
            }

            Image img;
            ThumbnailSource source = ThumbnailSource::Unknown;
            Result res = generate_internal(path, options, img, &source);

            if (options.token.is_canceled()) {
                events_.push(ThumbnailCanceled{id, path, options.size});
            } else if (res) {
                events_.push(ThumbnailReady{id, path, options.size, source, std::move(img)});
            } else {
                if (config_.record_failures) {
                    cache_.record_failure(path);
                }
                events_.push(ThumbnailFailed{id, path, options.size, res.error});
            }

            cleanup_active(id);
        });

        return id;
    }

    void cancel(RequestId request_id) override {
        pool_.cancel(request_id);
        std::lock_guard<std::mutex> lock(active_mutex_);
        auto it = active_tokens_.find(request_id);
        if (it != active_tokens_.end()) {
            it->second.cancel();
        }
    }

    Result get_sync(const std::filesystem::path& path, const ThumbnailOptions& options,
                    Image& out_image, ThumbnailSource* out_source) override {
        ThumbnailSource src = ThumbnailSource::Unknown;
        Result res = generate_internal(path, options, out_image, &src);
        if (out_source) *out_source = src;
        if (!res && config_.record_failures) {
            cache_.record_failure(path);
        }
        return res;
    }

    MessageQueue<ThumbnailEvent>& events() override {
        return events_;
    }

    PlatformCapabilities capabilities() const override {
        PlatformCapabilities caps = NativeThumbnailExtractor::capabilities();
        caps.has_xdg_cache = true;
        // The built-in generators' types, and their extensions as the platform's type database
        // spells them.
        std::vector<std::string> types = image_generator_.supported_mime_types();
        for (auto& t : text_generator_.supported_mime_types()) types.push_back(std::move(t));
        if (PdfThumbnailGenerator::is_available()) types.push_back("application/pdf");
        caps.supported_mime_types = types;
        for (const auto& e : detail::extensions_for_types(types)) {
            if (std::find(caps.supported_extensions.begin(),
                          caps.supported_extensions.end(), e) == caps.supported_extensions.end()) {
                caps.supported_extensions.push_back(e);
            }
        }
        return caps;
    }

    ThumbnailCache& cache() override {
        return cache_;
    }

    const ThumbnailCache& cache() const override {
        return cache_;
    }

private:
    void cleanup_active(RequestId id) {
        std::lock_guard<std::mutex> lock(active_mutex_);
        active_tokens_.erase(id);
    }

    Result generate_internal(const std::filesystem::path& path,
                             const ThumbnailOptions& options,
                             Image& out_image,
                             ThumbnailSource* out_source) {
        // 1. Check cache if requested
        if (options.use_cache) {
            auto cached = cache_.lookup(path, options.size, true, true);
            if (cached.found && !cached.image.empty()) {
                out_image = std::move(cached.image);
                if (out_source) *out_source = ThumbnailSource::Cache;
                return Result::success();
            }
            if (cached.is_failed) {
                return Result::failure("Cached failure recorded for: " + path.string());
            }
        }

        if (options.token.is_canceled()) {
            return Result::failure("Canceled");
        }

        int32_t px = to_pixels(options.size);
        bool generated = false;
        ThumbnailSource src = ThumbnailSource::Unknown;

        // One type decision (name and content, brovfs) picks the generators below and is
        // recorded as the cached thumbnail's Thumb::Mimetype.
        const std::string mime = detail::resolve_type(path, "");

        // 2. Try OS native if preferred
        if (options.prefer_native && config_.enable_native && NativeThumbnailExtractor::is_supported()) {
            Result native_res = NativeThumbnailExtractor::extract(path, px, out_image);
            if (native_res && !out_image.empty()) {
                generated = true;
                src = ThumbnailSource::NativeShell;
            }
        }

        if (options.token.is_canceled()) {
            return Result::failure("Canceled");
        }

        // 3. Try Image generator
        if (!generated && image_generator_.can_generate(path, mime)) {
            Result img_res = image_generator_.generate(path, px, out_image);
            if (img_res && !out_image.empty()) {
                generated = true;
                src = ThumbnailSource::GeneratorImage;
            }
        }

        if (options.token.is_canceled()) {
            return Result::failure("Canceled");
        }

        // 4. Try PDF generator
        if (!generated && pdf_generator_.can_generate(path, mime)) {
            Result pdf_res = pdf_generator_.generate(path, px, out_image);
            if (pdf_res && !out_image.empty()) {
                generated = true;
                src = ThumbnailSource::GeneratorPdf;
            }
        }

        if (options.token.is_canceled()) {
            return Result::failure("Canceled");
        }

        // 5. Try Text generator
        if (!generated && text_generator_.can_generate(path, mime)) {
            Result txt_res = text_generator_.generate(path, px, out_image);
            if (txt_res && !out_image.empty()) {
                generated = true;
                src = ThumbnailSource::GeneratorText;
            }
        }

        // 6. If still not generated and native wasn't tried earlier, try native as fallback
        if (!generated && !options.prefer_native && config_.enable_native &&
            NativeThumbnailExtractor::is_supported()) {
            Result native_res = NativeThumbnailExtractor::extract(path, px, out_image);
            if (native_res && !out_image.empty()) {
                generated = true;
                src = ThumbnailSource::NativeShell;
            }
        }

        if (!generated) {
            return Result::failure("No suitable generator could produce a thumbnail for: " + path.string());
        }

        if (out_source) *out_source = src;

        // 7. Store in cache if requested
        if (options.store_cache && src != ThumbnailSource::Cache) {
            ThumbnailMetadata meta;
            meta.mimetype = mime;
            cache_.store(path, options.size, out_image, meta);
        }

        return Result::success();
    }

    Config config_;
    ThumbnailCache cache_;
    WorkerThreadPool pool_;
    MessageQueue<ThumbnailEvent> events_;

    ImageThumbnailGenerator image_generator_;
    TextThumbnailGenerator text_generator_;
    PdfThumbnailGenerator pdf_generator_;

    std::atomic<RequestId> next_id_{0};
    std::mutex active_mutex_;
    std::unordered_map<RequestId, CancellationToken> active_tokens_;
};

}  // namespace

std::unique_ptr<ThumbnailService> ThumbnailService::create(const Config& config,
                                                           std::string* err) {
    (void)err;
    return std::make_unique<ThumbnailServiceImpl>(config);
}

std::unique_ptr<ThumbnailService> ThumbnailService::create() {
    return create(Config());
}

}  // namespace brothumb
