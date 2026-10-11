// Thumbnails for a folder through the service's own pool, end to end: what
// importing a camera card costs, and whether the pool keeps every worker busy
// (reading one photo while decoding another).
//
//   brothumb_bench_folder <dir>... [--threads N] [--size normal|large|xlarge|xxlarge] [--repeat K]
//
// Not a test (it needs photos). No cache is read or written. --repeat queues
// each file K times, to stand in for a larger folder. "Cold" is the caller's
// job: drop the files from the page cache first (e.g. `dd if=f iflag=nocache
// count=0` per file on Linux).

#include "brothumb/service.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace fs = std::filesystem;
using namespace brothumb;

namespace {

double cpu_seconds() {
#if defined(__unix__) || defined(__APPLE__)
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
    return double(u.ru_utime.tv_sec + u.ru_stime.tv_sec) + double(u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
#else
    return 0;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<fs::path> files;
    size_t threads = 0;
    int repeat = 1;
    ThumbnailSize size = ThumbnailSize::Large;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--threads" && i + 1 < argc) threads = size_t(std::atoi(argv[++i]));
        else if (a == "--repeat" && i + 1 < argc) repeat = std::max(1, std::atoi(argv[++i]));
        else if (a == "--size" && i + 1 < argc) {
            const std::string s = argv[++i];
            size = s == "normal" ? ThumbnailSize::Normal : s == "xlarge" ? ThumbnailSize::XLarge
                 : s == "xxlarge" ? ThumbnailSize::XXLarge : ThumbnailSize::Large;
        } else if (fs::is_directory(a)) {
            for (const auto& e : fs::recursive_directory_iterator(a))
                if (e.is_regular_file()) files.push_back(e.path());
        } else {
            files.emplace_back(a);
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::fprintf(stderr, "usage: brothumb_bench_folder <dir>... [--threads N] [--size large] [--repeat K]\n");
        return 2;
    }

    ThumbnailService::Config cfg;
    cfg.thread_count = threads;
    cfg.cache_dir = fs::temp_directory_path() / "brothumb_bench_folder_cache";
    cfg.record_failures = false;
    auto svc = ThumbnailService::create(cfg);
    const size_t pool = threads ? threads : ThumbnailService::default_thread_count();

    ThumbnailOptions o;
    o.size = size;
    o.use_cache = false;
    o.store_cache = false;
    o.prefer_native = false;

    const double cpu0 = cpu_seconds();
    const auto t0 = std::chrono::steady_clock::now();
    size_t queued = 0;
    for (int r = 0; r < repeat; ++r)
        for (const auto& f : files) {
            svc->request(f, o);
            ++queued;
        }
    size_t ready = 0, failed = 0;
    std::vector<std::string> failures;
    while (ready + failed < queued) {
        svc->events().wait_for(std::chrono::milliseconds(100));
        for (auto& ev : svc->events().drain()) {
            if (std::holds_alternative<ThumbnailReady>(ev)) ++ready;
            else if (auto* f = std::get_if<ThumbnailFailed>(&ev)) {
                ++failed;
                if (failures.size() < 5) failures.push_back(f->path.filename().string() + ": " + f->error);
            } else ++failed;
        }
    }
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double cpu = cpu_seconds() - cpu0;
    std::printf("%zu thumbnails (%zu files x %d), %zu failed, %zu workers: %.2f s wall, %.1f ms/thumbnail, "
                "%.1f thumbnails/s, cpu %.1f s (%.0f%% of the pool)\n",
                queued, files.size(), repeat, failed, pool, wall, 1000.0 * wall / double(queued),
                double(queued) / wall, cpu, cpu > 0 ? 100.0 * cpu / (wall * double(pool)) : 0.0);
    for (const auto& f : failures) std::printf("  failed: %s\n", f.c_str());
    return failed ? 1 : 0;
}
