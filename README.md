# brothumb

[![CI](https://github.com/wlejon/brothumb/actions/workflows/ci.yml/badge.svg)](https://github.com/wlejon/brothumb/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Thumbnail service substrate for desktop environments: Freedesktop XDG thumbnail
cache management, OS-native thumbnail extractors (Windows Shell, macOS Quick Look),
and built-in procedural and image generators. A standalone C++20 library with its
own CMake and ctest suite, with no dependency on bro or bronze core runtimes.

Part of the **[bro ecosystem](https://github.com/wlejon/bro/blob/main/docs/ecosystem.md)**.
It builds on three sibling libraries:

- **[brovfs](https://github.com/wlejon/brovfs)** — decides what type a file is from
  content and name together, using the platform's type database (`shared-mime-info`
  on Linux, `UTType` on macOS, the Windows registry via `AssocQueryString`). The MIME
  type routes the file to the appropriate generator and is recorded in the thumbnail's
  `Thumb::Mimetype` tag.
- **[broimage](https://github.com/wlejon/broimage)** — decodes raster images (PNG,
  JPEG, GIF, BMP, TGA, PSD, HDR, binary PNM; EXIF orientation applied), resizes them
  (area filter when shrinking, using premultiplied alpha to prevent dark fringing),
  and encodes cached PNGs with Freedesktop `Thumb::*` text chunks. Configured lean
  for brothumb (no tensor adapter, no JS API, no JIT compiler).
- **[bromath](https://github.com/wlejon/bromath)** — header-only vector and color
  math required by broimage.

## Platform Support

Stated honestly, what was verified where:

| Component | Linux | Windows | macOS |
| :--- | :--- | :--- | :--- |
| **XDG Thumbnail Cache** | `$XDG_CACHE_HOME/thumbnails` (`normal`, `large`, `x-large`, `xx-large`, `fail`) with strict `0600` permissions | `%LOCALAPPDATA%/thumbnails` or `%USERPROFILE%/.cache/thumbnails` | `~/.cache/thumbnails` |
| **Canonical URI & Hashes** | RFC 3986 percent-encoding + MD5 (`<hash>.png`) & SHA-256 | Drive-letter normalization (`file:///C:/...`) + MD5 | Firmlink-safe logical path normalization + MD5 |
| **PNG Metadata** | `Thumb::URI`, `Thumb::MTime`, `Thumb::Size`, `Thumb::Mimetype`, `Software` | Same | Same |
| **Native Shell Extractor** | Native XDG cache and desktop thumbnailers | Windows Shell `IShellItemImageFactory` / `IThumbnailCache` (COM) | Quick Look `QLThumbnailImageCreate` / `ImageIO` |
| **PDF First Page** | Headless `pdftoppm` (`poppler-utils`) | Windows Shell / WinRT PDF provider | PDFKit (`PDFDocument`, `PDFPage`) |
| **Image Generator** | broimage: PNG, JPEG, GIF, BMP, TGA, PSD, HDR, binary PNM (P5/P6); custom decoders registered by extension | Same | Same |
| **Text Preview Generator** | Stylized preview card with embedded monospace font, syntax highlighting, gutter line numbers, and extension badge | Same | Same |
| **Worker Pool & Events** | Priority queue (High, Normal, Low), cancellation tokens, host thread snapshot draining via `MessageQueue` | Same | Same |

### Verified Configurations

- **Linux**: Ubuntu 24.04 LTS (x86_64), GCC 14, Clang 18, with `poppler-utils`, `shared-mime-info`, and `file(1)`.
- **Windows**: Windows Server 2022 (x64), MSVC 2022 (v143), Release and Debug CRT.
- **macOS**: macOS 15 Sequoia (arm64), Apple Clang 16, PDFKit, Quick Look.

## API Overview

```
include/brothumb/
  common.h          Result, Image (straight RGBA8), ThumbnailSize, ThumbnailOptions, PlatformCapabilities
  event_queue.h     MessageQueue<T> (thread-safe, multi-producer single-consumer)
  uri.h             Canonical RFC 3986 / Freedesktop file:// URI conversion, MD5 & SHA-256
  metadata.h        PNG metadata tags reading/writing (Thumb::URI, Thumb::MTime, Thumb::Size, Thumb::Mimetype)
  cache.h           ThumbnailCache: XDG thumbnail cache spec (lookup, store, fail, invalidate)
  generator.h       Built-in generators: Image (broimage formats), Text preview card, PDF
  native.h          OS native thumbnail extractors (Windows Shell, macOS Quick Look)
  pool.h            WorkerThreadPool (priority queue, cancellation tokens)
  service.h         ThumbnailService: async pipeline, event queue, platform capabilities
```

### Usage Example

```cpp
#include <brothumb/service.h>
#include <iostream>

int main() {
    std::string err;
    auto service = brothumb::ThumbnailService::create({}, &err);
    if (!service) {
        std::cerr << "Failed to init thumbnail service: " << err << "\n";
        return 1;
    }

    // Wake callback can post to the host event loop (e.g. GLFW, Qt, custom)
    service->events().set_wake([] { /* post wake event */ });

    // Request thumbnail asynchronously
    brothumb::ThumbnailOptions opts;
    opts.size = brothumb::ThumbnailSize::Normal; // 128x128 (Large=256, XLarge=512, XXLarge=1024)
    opts.priority = brothumb::Priority::Normal;
    brothumb::RequestId id = service->request("/path/to/photo.jpg", opts);

    // Synchronous fetch is also available when needed:
    // auto result = service->get_sync("/path/to/photo.jpg", opts);

    // On the host thread (e.g. main/render loop):
    for (auto& ev : service->events().drain()) {
        if (auto* ready = std::get_if<brothumb::ThumbnailReady>(&ev)) {
            if (ready->request_id == id) {
                std::cout << "Thumbnail ready: " << ready->image.width << "x"
                          << ready->image.height << " (" << ready->image.pixels.size() << " bytes)\n";
            }
        } else if (auto* failed = std::get_if<brothumb::ThumbnailFailed>(&ev)) {
            std::cerr << "Thumbnail failed: " << failed->error << "\n";
        }
    }
}
```

## Building

### Dependencies

There are no submodules: brovfs, broimage, bromath (and bronze, for the JavaScript API) are
`bro_dependency()` pins in `CMakeLists.txt`, resolved through `cmake/bro_deps.cmake` in this
order:
1. An existing CMake target (`brovfs`, `broimage`, `bromath`) a parent superbuild already added;
2. A working tree beside the top-level project (`../brovfs`, `../broimage`, `../bromath`), or
   `-DFETCHCONTENT_SOURCE_DIR_<NAME>=<path>`;
3. The head of its main branch, fetched from GitHub at configure, so a plain `git clone` builds.

### Consuming `brothumb` in CMake

Consumers link the `brothumb::brothumb` alias target:

```cmake
add_subdirectory(path/to/brothumb)   # or bro_dependency(brothumb ...)
target_link_libraries(my_app PRIVATE brothumb::brothumb)
```

### Build Commands

#### Linux (GCC 12+ or Clang, Ninja)

```bash
sudo apt install cmake ninja-build pkg-config shared-mime-info poppler-utils file
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel 4
ctest --test-dir build-release --output-on-failure -j 1
```

#### Windows (MSVC 2022)

```powershell
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

#### macOS (Apple Clang 15+, Ninja)

```bash
brew install cmake ninja
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel 4
ctest --test-dir build-release --output-on-failure -j 1
```

### Build Options

- `-DBROTHUMB_BUILD_TESTS=ON|OFF` (default: ON when top-level): build test suite.
- `-DBROTHUMB_COVERAGE=ON|OFF` (default: OFF): instrument GCC/Clang with gcov (`--coverage -O0 -g`).
- `-DBROTHUMB_ENABLE_API=ON|OFF` (default: ON when top-level): build standalone Bronze JavaScript API (`brothumb_api`; bronze from `../bronze` or the head of its main branch).

## Tests

The test suite runs real system operations without mock shortcuts. Test failures are counted in all build configurations (no `assert()` reliance):

| Test | Coverage & Oracle |
| :--- | :--- |
| `test_uri` | Official Freedesktop test vectors, MD5 (RFC 1321), SHA-256 (FIPS 180-2), percent-encoding roundtrip |
| `test_metadata` | PNG metadata tags reading & writing (`Thumb::URI`, `Thumb::MTime`, `Thumb::Size`, UTF-8 custom tags via iTXt), cache freshness verification |
| `test_cache` | XDG directory hierarchy, atomic staging `.tmp` files and renames, exact lookup, downscaling fallback, failure recording (`fail/`) |
| `test_generators` | PNG, BMP, PPM, JPEG with aspect ratio preservation; routing by content type (extensionless PNG, text named `.png`, MIME aliases); premultiplied alpha color-fringe prevention; Text preview cards; PDF availability |
| `test_service_async` | Priority worker pool, cancellation tokens, host thread event queue drainage, `set_wake`, `get_sync`, `Thumb::Mimetype` from brovfs, capability types/extensions |
| `test_linux_native` | Freedesktop `0600` permissions, system `file(1)` oracle, XDG cache interoperability (Linux only) |
| `test_win_native` | Windows Shell `IShellItemImageFactory` / `IThumbnailCache` COM extraction on real files (Windows only) |
| `test_mac_native` | macOS Quick Look / ImageIO extraction vs `qlmanage -t` test oracle (macOS only) |
| `brothumb_test_api` | Bronze JavaScript API bindings with garbage collection stress testing (`BRONZE_GC_STRESS=1`, `BRONZE_GC_POISON=1`) |

### What Skips on CI and Why

Tests exit with code `77` when an environmental dependency is missing, which `ctest` reports as `Skipped` rather than `Passed` or `Failed`:

- **PDF extraction** in `test_generators`: Skips if `pdftoppm` (from `poppler-utils` on Linux) or the platform PDF provider is not found in PATH or the OS runtime.
- **Native shell extraction**:
  - `test_linux_native`: Skips if system `file(1)` is missing or if desktop thumbnailers are unavailable in headless CI containers without shared-mime-info.
  - `test_win_native`: Skips on Windows if COM fails to instantiate a thumbnail handler for the given file extension.
  - `test_mac_native`: Skips on macOS if `qlmanage` is unable to generate reference thumbnails in headless execution.
- **Bronze JavaScript API** (`brothumb_test_api`): Skips registration if the `brass` JIT compiler has no code generator backend for the host architecture.

## License

[MIT](LICENSE)
