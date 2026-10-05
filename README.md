# brothumb

[![CI](https://github.com/wlejon/brothumb/actions/workflows/ci.yml/badge.svg)](https://github.com/wlejon/brothumb/actions/workflows/ci.yml)

Thumbnail service substrate for a desktop environment built on the
[bro](https://github.com/wlejon/bro) runtime:
Freedesktop XDG thumbnail cache management, OS native thumbnail extractors (Windows Shell,
macOS Quick Look), and built-in procedural and image generators. A standalone C++20
library with its own CMake and ctest, and no dependency on bro or bronze. It builds on two
siblings:

- **[broimage](https://github.com/wlejon/broimage)** decodes (PNG, JPEG, GIF, BMP, TGA, PSD, HDR, PNM; EXIF orientation applied),
  resizes (area filter when shrinking, in premultiplied alpha) and encodes the cached PNGs,
  including their `Thumb::*` text chunks. It is configured lean: no JS API, tensor or JIT.
- **[brovfs](https://github.com/wlejon/brovfs)** decides what type a file is, from content and name together, using the
  platform's type database (shared-mime-info, UTType, the Windows registry). The type
  picks the generator and is recorded as `Thumb::Mimetype`.

broimage in turn needs [bromath](https://github.com/wlejon/bromath) (header-only). How
they are found is under [Building](#building).

## Model

The host interacts with `ThumbnailService` on its own thread:

```cpp
std::string err;
auto service = brothumb::ThumbnailService::create({}, &err);
service->events().set_wake([] { /* post to host event loop */ });

// Request thumbnail asynchronously
brothumb::ThumbnailOptions opts;
opts.size = brothumb::ThumbnailSize::Normal; // 128x128
brothumb::RequestId id = service->request("/path/to/file.png", opts);

// On the host thread (e.g. main/render loop):
for (auto& ev : service->events().drain()) {
    if (auto* ready = std::get_if<brothumb::ThumbnailReady>(&ev)) {
        render_thumbnail(ready->request_id, ready->image);
    } else if (auto* failed = std::get_if<brothumb::ThumbnailFailed>(&ev)) {
        log_error(failed->error);
    }
}
```

```
include/brothumb/
  common.h          Result, Image (straight RGBA8), ThumbnailSize, ThumbnailOptions, PlatformCapabilities
  event_queue.h     MessageQueue<T> (thread-safe, multi-producer single-consumer)
  uri.h             Canonical RFC 3986 / Freedesktop file:// URI conversion, MD5 & SHA-256
  metadata.h        PNG metadata tags reading/writing (Thumb::URI, Thumb::MTime, Thumb::Size)
  cache.h           ThumbnailCache: XDG thumbnail cache spec (lookup, store, fail, invalidate)
  generator.h       Built-in generators: Image (broimage formats), Text preview card, PDF
  native.h          OS native thumbnail extractors (Windows Shell, macOS Quick Look)
  pool.h            WorkerThreadPool (priority queue, cancellation tokens)
  service.h         ThumbnailService: async pipeline, event queue, platform capabilities
```

## Features & Backends

| Component | Linux | Windows | macOS |
|-----------|-------|---------|-------|
| **XDG Thumbnail Spec** | `$XDG_CACHE_HOME/thumbnails` (`normal`, `large`, `x-large`, `xx-large`, `fail`) with `0600` permissions | `%LOCALAPPDATA%/thumbnails` or `%USERPROFILE%/.cache/thumbnails` | `~/.cache/thumbnails` |
| **Canonical URI & Hashes** | RFC 3986 percent-encoding + MD5 (`<hash>.png`) & SHA-256 | Drive-letter normalization (`file:///C:/...`) + MD5 | Firmlink-safe logical path normalization + MD5 |
| **PNG Metadata** | `Thumb::URI`, `Thumb::MTime`, `Thumb::Size`, `Thumb::Mimetype`, `Software` | Same | Same |
| **Native Shell Extractor** | Native XDG cache and desktop thumbnailers | `IShellItemImageFactory` / `IThumbnailCache` (COM) | Quick Look `QLThumbnailImageCreate` / `ImageIO` |
| **PDF First Page** | Headless `pdftoppm` | Windows Shell / WinRT PDF provider | PDFKit (`PDFDocument`, `PDFPage`) |
| **Image Generator** | broimage: PNG, JPEG, GIF, BMP, TGA, PSD, HDR, binary PNM (P5/P6), chosen by brovfs type; custom decoders registered by extension | Same | Same |
| **Text Preview Generator** | Stylized preview card with embedded monospace font, syntax highlighting, gutter line numbers, and extension badge | Same | Same |
| **Worker Pool & Events** | Priority queue (High, Normal, Low), cancellation tokens, host thread snapshot draining via `MessageQueue` | Same | Same |

## Building

brothumb needs brovfs, broimage and bromath. It looks for checkouts beside it first, which
is how the sibling repos are developed together:

```bash
git clone https://github.com/wlejon/brothumb
git clone https://github.com/wlejon/brovfs     # each overridable with -D<NAME>_DIR=<path>,
git clone https://github.com/wlejon/broimage   # e.g. -DBROIMAGE_DIR=...
git clone https://github.com/wlejon/bromath
```

Any it does not find there it builds from the `third_party/` submodules:

```bash
git clone --recursive https://github.com/wlejon/brothumb
# or, in an existing clone:
git submodule update --init --recursive
```

Windows (MSVC, Visual Studio generator):

```bash
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Linux (GCC 12+, Arch / Debian / Fedora; Ninja):

```bash
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j 4
ctest --test-dir build-release --output-on-failure -j 1
```

macOS (Apple Clang, macOS 13+; Ninja):

```bash
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j 4
ctest --test-dir build-release --output-on-failure -j 1
```

## Tests

Real ctests that exercise the real OS, fail in Release (no `assert()`), and leave no lasting changes:

| Test | Coverage & Oracle |
|------|-------------------|
| `test_uri` | Official Freedesktop test vectors, MD5 (RFC 1321), SHA-256 (FIPS 180-2), percent-encoding roundtrip |
| `test_metadata` | PNG metadata tags reading & writing (`Thumb::URI`, `Thumb::MTime`, `Thumb::Size`, UTF-8 custom tags via iTXt), cache freshness verification |
| `test_cache` | XDG directory hierarchy, atomic stores, exact lookup, downscaling fallback, failure recording (`fail/`) |
| `test_generators` | PNG, BMP, PPM, JPEG with aspect ratio preservation; routing by content type (extensionless PNG, text named `.png`, MIME aliases); no colour fringe from transparent pixels when shrinking; Text preview cards; PDF availability |
| `test_service_async` | Priority worker pool, cancellation tokens, host thread event queue drainage, `set_wake`, `get_sync`, `Thumb::Mimetype` from brovfs, capability types/extensions |
| `test_win_native` | Windows Shell `IShellItemImageFactory` thumbnail extraction on real files |
| `test_mac_native` | macOS Quick Look / ImageIO extraction vs `qlmanage -t` test oracle |
| `test_linux_native` | Freedesktop `0600` permissions, system `file` oracle, XDG cache interoperability |
