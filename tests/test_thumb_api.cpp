#include "../src/api/api.h"
#include "check.h"
#include "embed/embed.h"
#include "eval/eval.h"
#include "brothumb/common.h"
#include "brothumb/service.h"

#include <broimage/encode.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void create_dummy_png(const std::filesystem::path& path, int width = 128, int height = 128, uint8_t color = 180) {
    std::filesystem::create_directories(path.parent_path());
    std::vector<uint8_t> rgba(static_cast<size_t>(width * height * 4), color);
    std::vector<uint8_t> png;
    broimage::encode_png_memory(png, rgba.data(), width, height, 4);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

void create_dummy_txt(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::trunc);
    f << text;
}

bool pumpUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    return bttest::wait_until([&] {
        brothumb::api::tickThumbAsync();
        bronze::embed::drainMicrotasks();
        return condition();
    }, timeout);
}

} // namespace

int main() {
    namespace ev = bronze::embed;
    using namespace bronze::eval;

    std::cout << "Starting brothumb JavaScript API test..." << std::endl;

    auto tmp_dir = std::filesystem::temp_directory_path() /
                   ("brothumb_api_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tmp_dir);

    auto test_cache_dir = tmp_dir / "cache";
    std::filesystem::create_directories(test_cache_dir);

    auto test_img1 = tmp_dir / "test1.png";
    auto test_img2 = tmp_dir / "test2.png";
    auto test_txt1 = tmp_dir / "test1.txt";

    create_dummy_png(test_img1, 256, 256, 120);
    create_dummy_png(test_img2, 256, 256, 200);
    create_dummy_txt(test_txt1, "Hello from brothumb API unit test suite!\nSecond line of preview.");

    struct Cleaner {
        std::filesystem::path dir;
        ~Cleaner() {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
    } cleaner{tmp_dir};

    // 1. Configure custom service pointing to isolated test cache
    brothumb::ThumbnailService::Config config;
    config.thread_count = 2;
    config.cache_dir = test_cache_dir;
    config.enable_native = true;
    config.record_failures = true;

    auto custom_service = brothumb::ThumbnailService::create(config);
    REQUIRE(custom_service != nullptr);
    brothumb::api::setService(std::move(custom_service));

    // 2. Install bro.thumb into Bronze realm
    brothumb::api::installThumb();

    auto g = ev::globalValue("bro");
    CHECK(g.found);
    CHECK(ev::isObject(g.value));

    ev::Persistent thumb(ev::getProperty(g.value, "thumb"));
    CHECK(ev::isObject(thumb.get()));
    std::cout << "  Mounted bro.thumb successfully." << std::endl;

    // Verify all functions exist
    const char* expectedMethods[] = {
        "get", "getSync", "invalidate", "getThumbnailPath", "getCapabilities",
        "cancel", "hasFailed", "recordFailure", "clearFailure", "clearCache", "getBaseDir"
    };
    for (const char* m : expectedMethods) {
        auto fn = ev::getProperty(thumb.get(), m);
        CHECK(ev::isFunction(fn));
        std::cout << "  Found bro.thumb." << m << std::endl;
    }

    // 3. Test getBaseDir()
    {
        auto r = evalScript("bro.thumb.getBaseDir()");
        CHECK(!r.thrown);
        CHECK(ev::isString(r.value));
        std::string baseDir = ev::toUtf8(r.value);
        CHECK_EQ(baseDir, test_cache_dir.string());
        std::cout << "  getBaseDir [PASS]" << std::endl;
    }

    // 4. Test getCapabilities()
    {
        auto r = evalScript(
            "(function() {\n"
            "  const caps = bro.thumb.getCapabilities();\n"
            "  if (typeof caps !== 'object' || caps === null) return false;\n"
            "  if (caps.canExtractImages !== true) return false;\n"
            "  if (caps.canExtractText !== true) return false;\n"
            "  if (typeof caps.canExtractPdfs !== 'boolean') return false;\n"
            "  if (typeof caps.canExtractNative !== 'boolean') return false;\n"
            "  if (typeof caps.hasXdgCache !== 'boolean') return false;\n"
            "  if (!Array.isArray(caps.supportedExtensions)) return false;\n"
            "  if (!Array.isArray(caps.supportedMimeTypes)) return false;\n"
            "  return true;\n"
            "})()\n"
        );
        CHECK(!r.thrown);
        CHECK(ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  getCapabilities [PASS]" << std::endl;
    }

    // 5. Test getThumbnailPath(path, options?)
    {
        std::string script =
            "(function() {\n"
            "  const p1 = bro.thumb.getThumbnailPath('" + test_img1.string() + "');\n"
            "  const p2 = bro.thumb.getThumbnailPath('" + test_img1.string() + "', { size: 'large' });\n"
            "  const p3 = bro.thumb.getThumbnailPath('" + test_img1.string() + "', { size: 512 });\n"
            "  if (typeof p1 !== 'string' || !p1.includes('normal')) return false;\n"
            "  if (typeof p2 !== 'string' || !p2.includes('large')) return false;\n"
            "  if (typeof p3 !== 'string' || !p3.includes('x-large')) return false;\n"
            "  return true;\n"
            "})()\n";
        auto r = evalScript(script);
        CHECK(!r.thrown);
        CHECK(ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  getThumbnailPath [PASS]" << std::endl;
    }

    // 6. Test getSync(path, options?)
    std::cout << "Testing bro.thumb.getSync..." << std::endl;
    {
        // 6a. Generate thumbnail for image (normal 128x128)
        std::string s1 =
            "(function() {\n"
            "  const res = bro.thumb.getSync('" + test_img1.string() + "');\n"
            "  if (!res) return false;\n"
            "  if (res.width !== 128 || res.height !== 128) return false;\n"
            "  if (!(res.pixels instanceof Uint8Array)) return false;\n"
            "  if (res.pixels.length !== 128 * 128 * 4) return false;\n"
            "  if (typeof res.source !== 'string' || res.source.length === 0) return false;\n"
            "  if (typeof res.path !== 'string' || !res.path.endsWith('.png')) return false;\n"
            "  return true;\n"
            "})()\n";
        auto r1 = evalScript(s1);
        CHECK(!r1.thrown);
        CHECK(ev::isBool(r1.value) && ev::toBool(r1.value));
        std::cout << "  getSync image 128x128 [PASS]" << std::endl;

        // 6b. Second call: should come from cache
        std::string s2 =
            "(function() {\n"
            "  const res = bro.thumb.getSync('" + test_img1.string() + "');\n"
            "  if (!res) return false;\n"
            "  return res.source === 'cache';\n"
            "})()\n";
        auto r2 = evalScript(s2);
        CHECK(!r2.thrown);
        CHECK(ev::isBool(r2.value) && ev::toBool(r2.value));
        std::cout << "  getSync from cache [PASS]" << std::endl;

        // 6c. Size option large (256x256)
        std::string sLarge =
            "(function() {\n"
            "  const res = bro.thumb.getSync('" + test_img1.string() + "', { size: 'large' });\n"
            "  if (!res) return false;\n"
            "  return res.width === 256 && res.height === 256 && res.pixels.length === 256 * 256 * 4;\n"
            "})()\n";
        auto rLarge = evalScript(sLarge);
        CHECK(!rLarge.thrown);
        CHECK(ev::isBool(rLarge.value) && ev::toBool(rLarge.value));
        std::cout << "  getSync size large [PASS]" << std::endl;

        // 6d. Text generator preview
        std::string sTxt =
            "(function() {\n"
            "  const res = bro.thumb.getSync('" + test_txt1.string() + "', { size: 'normal' });\n"
            "  if (!res) return false;\n"
            "  return res.width === 128 && res.height === 128 && res.pixels.length === 128 * 128 * 4;\n"
            "})()\n";
        auto rTxt = evalScript(sTxt);
        CHECK(!rTxt.thrown);
        CHECK(ev::isBool(rTxt.value) && ev::toBool(rTxt.value));
        std::cout << "  getSync text generator [PASS]" << std::endl;

        // 6e. cacheOnly option
        std::string sCacheOnly =
            "(function() {\n"
            "  // test_img1 is in cache: should succeed\n"
            "  const res1 = bro.thumb.getSync('" + test_img1.string() + "', { cacheOnly: true });\n"
            "  if (!res1 || res1.source !== 'cache') return false;\n"
            "  // test_img2 is not yet in cache: should return null\n"
            "  const res2 = bro.thumb.getSync('" + test_img2.string() + "', { cacheOnly: true });\n"
            "  if (res2 !== null) return false;\n"
            "  return true;\n"
            "})()\n";
        auto rCacheOnly = evalScript(sCacheOnly);
        CHECK(!rCacheOnly.thrown);
        CHECK(ev::isBool(rCacheOnly.value) && ev::toBool(rCacheOnly.value));
        std::cout << "  getSync cacheOnly [PASS]" << std::endl;

        // 6f. Nonexistent file returns null
        auto rNonExistent = evalScript("bro.thumb.getSync('/tmp/non_existent_file_xyz_123.png') === null;");
        CHECK(!rNonExistent.thrown);
        CHECK(ev::isBool(rNonExistent.value) && ev::toBool(rNonExistent.value));
        std::cout << "  getSync nonexistent returns null [PASS]" << std::endl;
    }

    // 7. Test async get(path, options?)
    std::cout << "Testing async bro.thumb.get..." << std::endl;
    {
        // 7a. Async get on test_img2
        std::string sAsync =
            "(function() {\n"
            "  globalThis._asyncSuccess = false;\n"
            "  globalThis._asyncRes = null;\n"
            "  globalThis._asyncErr = null;\n"
            "  const p = bro.thumb.get('" + test_img2.string() + "', { size: 'normal' });\n"
            "  if (!(p instanceof Promise)) return false;\n"
            "  p.then((res) => {\n"
            "    globalThis._asyncSuccess = true;\n"
            "    globalThis._asyncRes = res;\n"
            "  }).catch((err) => {\n"
            "    globalThis._asyncErr = String(err);\n"
            "  });\n"
            "  return true;\n"
            "})()\n";
        auto rInit = evalScript(sAsync);
        CHECK(!rInit.thrown);
        CHECK(ev::isBool(rInit.value) && ev::toBool(rInit.value));

        bool pumped = pumpUntil([&] {
            auto r = evalScript("globalThis._asyncSuccess === true || globalThis._asyncErr !== null;");
            return !r.thrown && ev::toBool(r.value);
        });
        CHECK(pumped);

        auto rCheckAsync = evalScript(
            "(function() {\n"
            "  if (!globalThis._asyncSuccess) return false;\n"
            "  const res = globalThis._asyncRes;\n"
            "  if (!res) return false;\n"
            "  if (res.width !== 128 || res.height !== 128) return false;\n"
            "  if (!(res.pixels instanceof Uint8Array)) return false;\n"
            "  if (res.pixels.length !== 128 * 128 * 4) return false;\n"
            "  if (typeof res.source !== 'string' || res.source.length === 0) return false;\n"
            "  if (typeof res.path !== 'string' || !res.path.endsWith('.png')) return false;\n"
            "  return true;\n"
            "})()\n"
        );
        CHECK(!rCheckAsync.thrown);
        CHECK(ev::isBool(rCheckAsync.value) && ev::toBool(rCheckAsync.value));
        std::cout << "  async get [PASS]" << std::endl;

        // 7b. Async failure on nonexistent file
        std::string sFail =
            "(function() {\n"
            "  globalThis._failRejected = false;\n"
            "  bro.thumb.get('/tmp/non_existent_brothumb_xyz.png').then(() => {\n"
            "    globalThis._failRejected = false;\n"
            "  }).catch(() => {\n"
            "    globalThis._failRejected = true;\n"
            "  });\n"
            "  return true;\n"
            "})()\n";
        auto rFailInit = evalScript(sFail);
        CHECK(!rFailInit.thrown);

        bool failPumped = pumpUntil([&] {
            auto r = evalScript("globalThis._failRejected === true;");
            return !r.thrown && ev::toBool(r.value);
        });
        CHECK(failPumped);
        std::cout << "  async get rejection on missing file [PASS]" << std::endl;
    }

    // 8. Test invalidate(path) and clearCache()
    std::cout << "Testing invalidate and clearCache..." << std::endl;
    {
        // First verify test_img2 is cached
        auto rCheckCache1 = evalScript("bro.thumb.getSync('" + test_img2.string() + "', { cacheOnly: true }) !== null;");
        CHECK(!rCheckCache1.thrown && ev::toBool(rCheckCache1.value));

        // Invalidate test_img2
        auto rInv = evalScript("bro.thumb.invalidate('" + test_img2.string() + "');");
        CHECK(!rInv.thrown && ev::toBool(rInv.value));

        // Now lookup should return null
        auto rCheckCache2 = evalScript("bro.thumb.getSync('" + test_img2.string() + "', { cacheOnly: true }) === null;");
        CHECK(!rCheckCache2.thrown && ev::toBool(rCheckCache2.value));
        std::cout << "  invalidate [PASS]" << std::endl;

        // clearCache()
        auto rClear = evalScript("bro.thumb.clearCache();");
        CHECK(!rClear.thrown && ev::toBool(rClear.value));
        std::cout << "  clearCache [PASS]" << std::endl;
    }

    // 9. Test failure tracking: hasFailed, recordFailure, clearFailure
    std::cout << "Testing failure recording..." << std::endl;
    {
        auto failedFile = tmp_dir / "failed_item.png";
        create_dummy_png(failedFile, 64, 64, 99);
        std::string testPath = failedFile.string();

        auto rHas1 = evalScript("bro.thumb.hasFailed('" + testPath + "') === false;");
        CHECK(!rHas1.thrown && ev::toBool(rHas1.value));

        auto rRec = evalScript("bro.thumb.recordFailure('" + testPath + "') === true;");
        CHECK(!rRec.thrown && ev::toBool(rRec.value));

        auto rHas2 = evalScript("bro.thumb.hasFailed('" + testPath + "') === true;");
        CHECK(!rHas2.thrown && ev::toBool(rHas2.value));

        auto rClr = evalScript("bro.thumb.clearFailure('" + testPath + "') === true;");
        CHECK(!rClr.thrown && ev::toBool(rClr.value));

        auto rHas3 = evalScript("bro.thumb.hasFailed('" + testPath + "') === false;");
        CHECK(!rHas3.thrown && ev::toBool(rHas3.value));
        std::cout << "  hasFailed / recordFailure / clearFailure [PASS]" << std::endl;
    }

    // 10. GC stress loop: execute multiple sync and async thumb calls under GC stress
    std::cout << "Running GC stress tests..." << std::endl;
    {
        for (int i = 0; i < 15; ++i) {
            auto iterImg = tmp_dir / ("iter_" + std::to_string(i) + ".png");
            create_dummy_png(iterImg, 64, 64, static_cast<uint8_t>(50 + i * 10));

            // Synchronous call
            std::string syncCode =
                "(function() {\n"
                "  const res = bro.thumb.getSync('" + iterImg.string() + "');\n"
                "  if (!res || res.pixels.length !== res.width * res.height * 4) return false;\n"
                "  return true;\n"
                "})()\n";
            auto r = evalScript(syncCode);
            CHECK(!r.thrown && ev::toBool(r.value));

            // Asynchronous call
            std::string asyncCode =
                "(function() {\n"
                "  globalThis._gcDone = false;\n"
                "  bro.thumb.get('" + iterImg.string() + "').then((res) => {\n"
                "    globalThis._gcDone = (res && res.pixels.length === res.width * res.height * 4);\n"
                "  }).catch(() => {\n"
                "    globalThis._gcDone = false;\n"
                "  });\n"
                "  return true;\n"
                "})()\n";
            auto rA = evalScript(asyncCode);
            CHECK(!rA.thrown);

            bool ok = pumpUntil([&] {
                auto checkR = evalScript("globalThis._gcDone === true;");
                return !checkR.thrown && ev::toBool(checkR.value);
            });
            CHECK(ok);
        }
        std::cout << "  GC stress loop [PASS]" << std::endl;
    }

    // 11. Shutdown and cleanup
    brothumb::api::shutdownThumbAsync();
    std::cout << "All brothumb API tests PASSED!" << std::endl;

    return bttest::finish("brothumb_test_api");
}
