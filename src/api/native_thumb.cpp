#include "api.h"
#include "host_thumb_internal.h"
#include "brothumb/common.h"
#include "brothumb/service.h"

#include <filesystem>
#include <unordered_map>

namespace brothumb::api {

namespace {

struct PendingThumbRequest {
    ev::Persistent promise;
    ThumbnailOptions options;
};

std::unordered_map<RequestId, PendingThumbRequest> g_pending_requests;

ThumbnailOptions parseOptions(Value optVal, bool* outCacheOnly, bool* outForce) {
    ThumbnailOptions opts;
    if (outCacheOnly) *outCacheOnly = false;
    if (outForce) *outForce = false;

    if (!ev::isObject(optVal)) return opts;
    ev::Persistent optP(optVal);

    ev::Persistent sizeVal(ev::getProperty(optP.get(), "size"));
    if (ev::isString(sizeVal.get())) {
        opts.size = size_from_string(ev::toUtf8(sizeVal.get()));
    } else if (ev::isNumber(sizeVal.get())) {
        int32_t px = static_cast<int32_t>(ev::toDouble(sizeVal.get()));
        opts.size = size_from_pixels(px);
    }

    ev::Persistent prioVal(ev::getProperty(optP.get(), "priority"));
    if (ev::isString(prioVal.get())) {
        std::string p = ev::toUtf8(prioVal.get());
        if (p == "low") opts.priority = Priority::Low;
        else if (p == "high") opts.priority = Priority::High;
        else opts.priority = Priority::Normal;
    } else if (ev::isNumber(prioVal.get())) {
        int32_t p = static_cast<int32_t>(ev::toDouble(prioVal.get()));
        if (p <= 0) opts.priority = Priority::Low;
        else if (p >= 2) opts.priority = Priority::High;
        else opts.priority = Priority::Normal;
    }

    ev::Persistent coVal(ev::getProperty(optP.get(), "cacheOnly"));
    if (ev::isBool(coVal.get()) && ev::toBool(coVal.get())) {
        if (outCacheOnly) *outCacheOnly = true;
    }

    ev::Persistent forceVal(ev::getProperty(optP.get(), "force"));
    if (ev::isBool(forceVal.get()) && ev::toBool(forceVal.get())) {
        if (outForce) *outForce = true;
        opts.use_cache = false;
    }

    ev::Persistent pnVal(ev::getProperty(optP.get(), "preferNative"));
    if (ev::isBool(pnVal.get())) {
        opts.prefer_native = ev::toBool(pnVal.get());
    }

    ev::Persistent scVal(ev::getProperty(optP.get(), "storeCache"));
    if (ev::isBool(scVal.get())) {
        opts.store_cache = ev::toBool(scVal.get());
    }

    return opts;
}

Value makeImageResult(ThumbnailService* svc, const std::filesystem::path& sourcePath,
                      ThumbnailSize size, ThumbnailSource source, const Image& img) {
    ev::Persistent pxP(typedArrayFrom(ev::elements::Uint8,
        img.rgba.data(), img.rgba.size(),
        static_cast<uint32_t>(img.rgba.size())));

    std::string thumbPath = svc->cache().thumbnail_path_for_source(sourcePath, size).string();

    ObjectBuilder b;
    b.set("width", static_cast<double>(img.width));
    b.set("height", static_cast<double>(img.height));
    b.set("pixels", pxP.get());
    b.set("source", std::string(to_string(source)));
    b.set("path", thumbPath);
    b.set("sourcePath", sourcePath.string());
    b.set("thumbnailPath", thumbPath);
    return b.get();
}

} // namespace

void installThumbOnto(Value thumbVal) {
    ObjectBuilder thumb(thumbVal);

    // bro.thumb.get(path, options?) -> Promise<{ width, height, pixels, source, path }>
    thumb.def("get", 1, [](Value, std::span<const Value> args) -> Value {
        ev::Persistent pathP(args.empty() ? ev::undefined() : args[0]);
        ev::Persistent optP(args.size() > 1 ? args[1] : ev::undefined());
        ev::Persistent promiseP(ev::createPromise());

        if (!ev::isString(pathP.get())) {
            ev::Persistent err(makeError("bro.thumb.get requires a file path string"));
            ev::rejectPromise(promiseP.get(), err.get());
            return promiseP.get();
        }

        std::filesystem::path sourcePath(ev::toUtf8(pathP.get()));
        bool cacheOnly = false;
        bool force = false;
        ThumbnailOptions opts = parseOptions(optP.get(), &cacheOnly, &force);

        ThumbnailService* svc = getService();
        if (!svc) {
            ev::Persistent err(makeError("Thumbnail service is not available"));
            ev::rejectPromise(promiseP.get(), err.get());
            return promiseP.get();
        }

        if (cacheOnly) {
            auto cached = svc->cache().lookup(sourcePath, opts.size, true, true);
            if (cached.found && !cached.image.empty()) {
                ev::Persistent resP(makeImageResult(svc, sourcePath, opts.size, ThumbnailSource::Cache, cached.image));
                ev::resolvePromise(promiseP.get(), resP.get());
            } else {
                ev::Persistent err(makeError("Thumbnail not found in cache for: " + sourcePath.string()));
                ev::rejectPromise(promiseP.get(), err.get());
            }
            return promiseP.get();
        }

        RequestId reqId = svc->request(sourcePath, opts);
        g_pending_requests[reqId] = PendingThumbRequest{
            .promise = promiseP,
            .options = opts
        };

        ev::Persistent reqIdVal(ev::fromDouble(static_cast<double>(reqId)));
        ev::setProperty(promiseP.get(), "requestId", reqIdVal.get());
        return promiseP.get();
    });

    // bro.thumb.getSync(path, options?) -> { width, height, pixels, source, path } | null
    thumb.def("getSync", 1, [](Value, std::span<const Value> args) -> Value {
        ev::Persistent pathP(args.empty() ? ev::undefined() : args[0]);
        ev::Persistent optP(args.size() > 1 ? args[1] : ev::undefined());

        if (!ev::isString(pathP.get())) {
            return ev::throwTypeError("bro.thumb.getSync requires a file path string");
        }

        std::filesystem::path sourcePath(ev::toUtf8(pathP.get()));
        bool cacheOnly = false;
        bool force = false;
        ThumbnailOptions opts = parseOptions(optP.get(), &cacheOnly, &force);

        ThumbnailService* svc = getService();
        if (!svc) return ev::null();

        if (cacheOnly) {
            auto cached = svc->cache().lookup(sourcePath, opts.size, true, true);
            if (cached.found && !cached.image.empty()) {
                return makeImageResult(svc, sourcePath, opts.size, ThumbnailSource::Cache, cached.image);
            }
            return ev::null();
        }

        Image outImage;
        ThumbnailSource outSource = ThumbnailSource::Unknown;
        Result res = svc->get_sync(sourcePath, opts, outImage, &outSource);
        if (!res.ok || outImage.empty()) {
            return ev::null();
        }

        return makeImageResult(svc, sourcePath, opts.size, outSource, outImage);
    });

    // bro.thumb.invalidate(path) -> boolean
    thumb.def("invalidate", 1, [](Value, std::span<const Value> args) -> Value {
        ev::Persistent pathP(args.empty() ? ev::undefined() : args[0]);
        if (!ev::isString(pathP.get())) {
            return ev::throwTypeError("bro.thumb.invalidate requires a file path string");
        }
        std::filesystem::path p(ev::toUtf8(pathP.get()));
        ThumbnailService* svc = getService();
        if (!svc) return ev::fromBool(false);
        Result r = svc->cache().invalidate(p);
        return ev::fromBool(r.ok);
    });

    // bro.thumb.getThumbnailPath(path, options?) -> string
    thumb.def("getThumbnailPath", 1, [](Value, std::span<const Value> args) -> Value {
        ev::Persistent pathP(args.empty() ? ev::undefined() : args[0]);
        ev::Persistent optP(args.size() > 1 ? args[1] : ev::undefined());

        if (!ev::isString(pathP.get())) {
            return ev::throwTypeError("bro.thumb.getThumbnailPath requires a file path string");
        }
        std::filesystem::path p(ev::toUtf8(pathP.get()));
        ThumbnailSize size = ThumbnailSize::Normal;
        if (ev::isObject(optP.get())) {
            ev::Persistent sVal(ev::getProperty(optP.get(), "size"));
            if (ev::isString(sVal.get())) {
                size = size_from_string(ev::toUtf8(sVal.get()));
            } else if (ev::isNumber(sVal.get())) {
                size = size_from_pixels(static_cast<int32_t>(ev::toDouble(sVal.get())));
            }
        }
        ThumbnailService* svc = getService();
        if (!svc) {
            return ev::throwError("Thumbnail service is not available");
        }
        std::string thumbPath = svc->cache().thumbnail_path_for_source(p, size).string();
        return ev::fromUtf8(thumbPath);
    });

    // bro.thumb.getCapabilities() -> { canExtractImages, canExtractPdfs, canExtractText, canExtractNative, ... }
    thumb.def("getCapabilities", 0, [](Value, std::span<const Value>) -> Value {
        ThumbnailService* svc = getService();
        PlatformCapabilities caps;
        if (svc) {
            caps = svc->capabilities();
        }
        ObjectBuilder b;
        b.set("canExtractImages", true);
        b.set("canExtractPdfs", caps.has_pdf_rendering);
        b.set("canExtractText", true);
        b.set("canExtractNative", caps.has_windows_shell || caps.has_macos_quicklook);
        b.set("hasXdgCache", caps.has_xdg_cache);
        b.set("pdfBackend", caps.pdf_backend);

        ev::Persistent extArr(ev::makeArray(static_cast<uint32_t>(caps.supported_extensions.size())));
        for (size_t i = 0; i < caps.supported_extensions.size(); ++i) {
            ev::Persistent sP(ev::fromUtf8(caps.supported_extensions[i]));
            extArr.set(ev::setElement(extArr.get(), static_cast<uint32_t>(i), sP.get()));
        }
        b.set("supportedExtensions", extArr.get());

        ev::Persistent mimeArr(ev::makeArray(static_cast<uint32_t>(caps.supported_mime_types.size())));
        for (size_t i = 0; i < caps.supported_mime_types.size(); ++i) {
            ev::Persistent sP(ev::fromUtf8(caps.supported_mime_types[i]));
            mimeArr.set(ev::setElement(mimeArr.get(), static_cast<uint32_t>(i), sP.get()));
        }
        b.set("supportedMimeTypes", mimeArr.get());

        return b.get();
    });

    // bro.thumb.cancel(requestId) -> void
    thumb.def("cancel", 1, [](Value, std::span<const Value> args) -> Value {
        ev::Persistent idVal(args.empty() ? ev::undefined() : args[0]);
        if (!ev::isNumber(idVal.get())) {
            return ev::undefined();
        }
        RequestId id = static_cast<RequestId>(ev::toDouble(idVal.get()));
        ThumbnailService* svc = getService();
        if (svc) {
            svc->cancel(id);
        }
        return ev::undefined();
    });

    // bro.thumb.hasFailed(path, appName?) -> boolean
    thumb.def("hasFailed", 1, [](Value, std::span<const Value> args) -> Value {
        ev::Persistent pathP(args.empty() ? ev::undefined() : args[0]);
        ev::Persistent appP(args.size() > 1 ? args[1] : ev::undefined());

        if (!ev::isString(pathP.get())) return ev::fromBool(false);
        std::filesystem::path p(ev::toUtf8(pathP.get()));
        std::string appName;
        if (ev::isString(appP.get())) {
            appName = ev::toUtf8(appP.get());
        }
        ThumbnailService* svc = getService();
        if (!svc) return ev::fromBool(false);
        return ev::fromBool(svc->cache().has_failed(p, appName));
    });

    // bro.thumb.recordFailure(path, appName?) -> boolean
    thumb.def("recordFailure", 1, [](Value, std::span<const Value> args) -> Value {
        ev::Persistent pathP(args.empty() ? ev::undefined() : args[0]);
        ev::Persistent appP(args.size() > 1 ? args[1] : ev::undefined());

        if (!ev::isString(pathP.get())) return ev::fromBool(false);
        std::filesystem::path p(ev::toUtf8(pathP.get()));
        std::string appName;
        if (ev::isString(appP.get())) {
            appName = ev::toUtf8(appP.get());
        }
        ThumbnailService* svc = getService();
        if (!svc) return ev::fromBool(false);
        return ev::fromBool(svc->cache().record_failure(p, appName).ok);
    });

    // bro.thumb.clearFailure(path, appName?) -> boolean
    thumb.def("clearFailure", 1, [](Value, std::span<const Value> args) -> Value {
        ev::Persistent pathP(args.empty() ? ev::undefined() : args[0]);
        ev::Persistent appP(args.size() > 1 ? args[1] : ev::undefined());

        if (!ev::isString(pathP.get())) return ev::fromBool(false);
        std::filesystem::path p(ev::toUtf8(pathP.get()));
        std::string appName;
        if (ev::isString(appP.get())) {
            appName = ev::toUtf8(appP.get());
        }
        ThumbnailService* svc = getService();
        if (!svc) return ev::fromBool(false);
        return ev::fromBool(svc->cache().clear_failure(p, appName).ok);
    });

    // bro.thumb.clearCache(path?) -> boolean
    thumb.def("clearCache", 0, [](Value, std::span<const Value> args) -> Value {
        ThumbnailService* svc = getService();
        if (!svc) return ev::fromBool(false);

        ev::Persistent pathP(args.empty() ? ev::undefined() : args[0]);
        if (ev::isString(pathP.get())) {
            std::filesystem::path p(ev::toUtf8(pathP.get()));
            return ev::fromBool(svc->cache().invalidate(p).ok);
        }
        ThumbnailSize sizes[] = {
            ThumbnailSize::Normal,
            ThumbnailSize::Large,
            ThumbnailSize::XLarge,
            ThumbnailSize::XXLarge
        };
        std::error_code ec;
        for (auto s : sizes) {
            auto d = svc->cache().directory_for_size(s);
            if (std::filesystem::exists(d, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(d, ec)) {
                    std::filesystem::remove(entry.path(), ec);
                }
            }
        }
        auto fdir = svc->cache().directory_for_fail();
        if (std::filesystem::exists(fdir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(fdir, ec)) {
                std::filesystem::remove(entry.path(), ec);
            }
        }
        return ev::fromBool(true);
    });

    // bro.thumb.getBaseDir() -> string
    thumb.def("getBaseDir", 0, [](Value, std::span<const Value>) -> Value {
        ThumbnailService* svc = getService();
        if (!svc) return ev::fromUtf8("");
        return ev::fromUtf8(svc->cache().base_dir().string());
    });
}

void drainThumbEvents() {
    ThumbnailService* svc = getService();
    if (!svc) return;

    auto events = svc->events().drain();
    for (auto& evItem : events) {
        if (const auto* r = std::get_if<ThumbnailReady>(&evItem)) {
            auto it = g_pending_requests.find(r->request_id);
            if (it != g_pending_requests.end()) {
                ev::Persistent p = std::move(it->second.promise);
                g_pending_requests.erase(it);

                ev::Persistent resP(makeImageResult(svc, r->path, r->size, r->source, r->image));
                ev::resolvePromise(p.get(), resP.get());
            }
        } else if (const auto* f = std::get_if<ThumbnailFailed>(&evItem)) {
            auto it = g_pending_requests.find(f->request_id);
            if (it != g_pending_requests.end()) {
                ev::Persistent p = std::move(it->second.promise);
                g_pending_requests.erase(it);

                std::string errStr = f->error.empty() ? "Thumbnail generation failed" : f->error;
                ev::Persistent err(makeError(errStr));
                ev::rejectPromise(p.get(), err.get());
            }
        } else if (const auto* c = std::get_if<ThumbnailCanceled>(&evItem)) {
            auto it = g_pending_requests.find(c->request_id);
            if (it != g_pending_requests.end()) {
                ev::Persistent p = std::move(it->second.promise);
                g_pending_requests.erase(it);

                ev::Persistent err(makeError("Thumbnail request canceled"));
                ev::rejectPromise(p.get(), err.get());
            }
        }
    }
}

void cancelAllPendingThumbRequests() {
    ThumbnailService* svc = getService();
    for (auto& [reqId, req] : g_pending_requests) {
        if (svc) {
            svc->cancel(reqId);
        }
        ev::Persistent err(makeError("Thumbnail service shut down"));
        ev::rejectPromise(req.promise.get(), err.get());
    }
    g_pending_requests.clear();
}

} // namespace brothumb::api
