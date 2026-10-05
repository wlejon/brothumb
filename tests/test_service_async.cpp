#include "brothumb/service.h"
#include "check.h"
#include "lodepng/lodepng.h"

#include <atomic>
#include <chrono>
#include <fstream>

namespace {

void create_dummy_png(const std::filesystem::path& path) {
    std::vector<uint8_t> rgba(256 * 256 * 4, 180);
    std::vector<uint8_t> png;
    lodepng::encode(png, rgba, 256, 256);
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(png.data()), png.size());
}

}  // namespace

int main() {
    auto temp_dir = std::filesystem::temp_directory_path();
    auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto private_cache = temp_dir / ("brothumb_svc_cache_" + nonce);
    auto test_img = temp_dir / ("brothumb_svc_img_" + nonce + ".png");
    auto test_txt = temp_dir / ("brothumb_svc_txt_" + nonce + ".txt");

    struct Cleaner {
        std::filesystem::path dir, a, b;
        ~Cleaner() {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
            std::filesystem::remove(a, ec);
            std::filesystem::remove(b, ec);
        }
    } cleaner{private_cache, test_img, test_txt};

    create_dummy_png(test_img);
    {
        std::ofstream f(test_txt);
        f << "Text preview for async thumbnail service test.\n";
    }

    // 1. Create service with private cache
    brothumb::ThumbnailService::Config config;
    config.thread_count = 2;
    config.cache_dir = private_cache;
    config.enable_native = true;
    config.record_failures = true;

    auto service = brothumb::ThumbnailService::create(config);
    REQUIRE(service != nullptr);

    std::atomic<int> wake_count{0};
    service->events().set_wake([&] {
        ++wake_count;
    });

    // 2. Test Async Request (Image)
    brothumb::ThumbnailOptions opts;
    opts.size = brothumb::ThumbnailSize::Normal;
    brothumb::RequestId req1 = service->request(test_img, opts);
    CHECK(req1 > 0);

    // Wait for event on host thread (wait_until)
    bool got_ready = bttest::wait_until([&] {
        return !service->events().empty();
    }, std::chrono::milliseconds(3000));
    CHECK(got_ready);

    // Drain events on host thread
    auto events = service->events().drain();
    CHECK(!events.empty());
    bool found_ready1 = false;
    for (const auto& ev : events) {
        if (const auto* r = std::get_if<brothumb::ThumbnailReady>(&ev)) {
            if (r->request_id == req1) {
                found_ready1 = true;
                CHECK_EQ(r->size, brothumb::ThumbnailSize::Normal);
                CHECK_EQ(r->image.width, 128);
                CHECK_EQ(r->image.height, 128);
            }
        }
    }
    CHECK(found_ready1);
    CHECK(wake_count > 0);

    // 3. Test Async Request (Text Preview)
    opts.size = brothumb::ThumbnailSize::Large;
    brothumb::RequestId req2 = service->request(test_txt, opts);

    bool got_ready2 = bttest::wait_until([&] {
        return !service->events().empty();
    }, std::chrono::milliseconds(3000));
    CHECK(got_ready2);

    auto events2 = service->events().drain();
    bool found_ready2 = false;
    for (const auto& ev : events2) {
        if (const auto* r = std::get_if<brothumb::ThumbnailReady>(&ev)) {
            if (r->request_id == req2) {
                found_ready2 = true;
                CHECK_EQ(r->size, brothumb::ThumbnailSize::Large);
                CHECK_EQ(r->image.width, 256);
                CHECK_EQ(r->image.height, 256);
            }
        }
    }
    CHECK(found_ready2);

    // 4. Test Cancellation
    brothumb::CancellationToken cancel_tok;
    cancel_tok.cancel(); // Pre-cancel
    brothumb::ThumbnailOptions cancel_opts;
    cancel_opts.token = cancel_tok;

    brothumb::RequestId req_cancel = service->request(test_img, cancel_opts);
    bool got_cancel = bttest::wait_until([&] {
        return !service->events().empty();
    }, std::chrono::milliseconds(2000));
    CHECK(got_cancel);

    auto cancel_events = service->events().drain();
    bool found_canceled = false;
    for (const auto& ev : cancel_events) {
        if (const auto* c = std::get_if<brothumb::ThumbnailCanceled>(&ev)) {
            if (c->request_id == req_cancel) {
                found_canceled = true;
            }
        }
    }
    CHECK(found_canceled);

    // 5. Test Failure Event
    auto non_existent = temp_dir / "non_existent_file_brothumb.xyz";
    brothumb::RequestId req_fail = service->request(non_existent, opts);
    bool got_fail = bttest::wait_until([&] {
        return !service->events().empty();
    }, std::chrono::milliseconds(2000));
    CHECK(got_fail);

    auto fail_events = service->events().drain();
    bool found_failed = false;
    for (const auto& ev : fail_events) {
        if (const auto* f = std::get_if<brothumb::ThumbnailFailed>(&ev)) {
            if (f->request_id == req_fail) {
                found_failed = true;
                CHECK(!f->error.empty());
            }
        }
    }
    CHECK(found_failed);

    // 6. Test Synchronous get_sync
    brothumb::Image sync_img;
    brothumb::ThumbnailSource sync_src = brothumb::ThumbnailSource::Unknown;
    opts.size = brothumb::ThumbnailSize::Normal;
    brothumb::Result sync_res = service->get_sync(test_img, opts, sync_img, &sync_src);
    CHECK(sync_res.ok);
    CHECK_EQ(sync_src, brothumb::ThumbnailSource::Cache);
    CHECK_EQ(sync_img.width, 128);
    CHECK_EQ(sync_img.height, 128);

    return bttest::finish("test_service_async");
}
