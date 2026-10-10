#include "brothumb/generator.h"
#include "brothumb/metadata.h"
#include "check.h"

#include <broimage/encode.h>
#include <broimage/heif.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>

namespace {

void write_bytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void create_test_png(const std::filesystem::path& path, int w, int h) {
    std::vector<uint8_t> rgba(w * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = (y * w + x) * 4;
            rgba[idx + 0] = static_cast<uint8_t>((x * 255) / w);
            rgba[idx + 1] = static_cast<uint8_t>((y * 255) / h);
            rgba[idx + 2] = 128;
            rgba[idx + 3] = 255;
        }
    }
    std::vector<uint8_t> png;
    broimage::encode_png_memory(png, rgba.data(), w, h, 4);
    write_bytes(path, png);
}

void create_test_bmp(const std::filesystem::path& path, int w, int h) {
    // 24-bit BMP
    int row_stride = ((w * 3 + 3) / 4) * 4;
    uint32_t file_size = 54 + row_stride * h;

    uint8_t header[54] = {
        'B', 'M',
        static_cast<uint8_t>(file_size & 0xff),
        static_cast<uint8_t>((file_size >> 8) & 0xff),
        static_cast<uint8_t>((file_size >> 16) & 0xff),
        static_cast<uint8_t>((file_size >> 24) & 0xff),
        0, 0, 0, 0, // reserved
        54, 0, 0, 0, // offset
        40, 0, 0, 0, // header size
        static_cast<uint8_t>(w & 0xff), static_cast<uint8_t>((w >> 8) & 0xff),
        static_cast<uint8_t>((w >> 16) & 0xff), static_cast<uint8_t>((w >> 24) & 0xff),
        static_cast<uint8_t>(h & 0xff), static_cast<uint8_t>((h >> 8) & 0xff),
        static_cast<uint8_t>((h >> 16) & 0xff), static_cast<uint8_t>((h >> 24) & 0xff),
        1, 0, // planes
        24, 0, // bpp
        0, 0, 0, 0, // uncompressed
        0, 0, 0, 0, // biSizeImage
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    std::vector<uint8_t> pixels(row_stride * h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = y * row_stride + x * 3;
            pixels[idx + 0] = static_cast<uint8_t>((x * 200) / w); // B
            pixels[idx + 1] = static_cast<uint8_t>((y * 200) / h); // G
            pixels[idx + 2] = 220;                                 // R
        }
    }

    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(header), 54);
    f.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
}

void create_test_ppm(const std::filesystem::path& path, int w, int h) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    std::vector<uint8_t> rgb(w * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = (y * w + x) * 3;
            rgb[idx + 0] = static_cast<uint8_t>((x * 255) / w);
            rgb[idx + 1] = static_cast<uint8_t>((y * 255) / h);
            rgb[idx + 2] = 100;
        }
    }
    f.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
}

// A big-endian, PackBits-compressed RGB TIFF of flat grey carrying EXIF
// orientation 6 in its own IFD0 (upright it is h x w).
void create_test_tiff(const std::filesystem::path& path, int w, int h) {
    std::vector<uint8_t> t = {'M', 'M', 0, 42, 0, 0, 0, 8};
    auto u16 = [&](uint32_t v) { t.push_back(uint8_t(v >> 8)); t.push_back(uint8_t(v)); };
    auto u32 = [&](uint32_t v) { u16(v >> 16); u16(v & 0xFFFF); };
    // PackBits: each row of w*3 bytes of 128 as repeat runs of up to 128.
    std::vector<uint8_t> strip;
    for (int y = 0; y < h; ++y)
        for (int left = w * 3; left > 0; left -= 128) {
            const int n = left < 128 ? left : 128;
            strip.push_back(uint8_t(1 - n));
            strip.push_back(128);
        }
    const uint32_t entries = 10, data_at = 8 + 2 + entries * 12 + 4, bps_at = data_at;
    const uint32_t strip_at = bps_at + 6;
    u16(entries);
    auto entry = [&](uint32_t tag, uint32_t type, uint32_t count, uint32_t value) {
        u16(tag); u16(type); u32(count);
        if (type == 3 && count == 1) { u16(value); u16(0); } else { u32(value); }
    };
    entry(256, 4, 1, uint32_t(w));
    entry(257, 4, 1, uint32_t(h));
    entry(258, 3, 3, bps_at);
    entry(259, 3, 1, 32773);
    entry(262, 3, 1, 2);
    entry(273, 4, 1, strip_at);
    entry(274, 3, 1, 6);
    entry(277, 3, 1, 3);
    entry(278, 4, 1, uint32_t(h));
    entry(279, 4, 1, uint32_t(strip.size()));
    u32(0);
    u16(8); u16(8); u16(8);
    t.insert(t.end(), strip.begin(), strip.end());
    write_bytes(path, t);
}

}  // namespace

int main() {
    auto temp_dir = std::filesystem::temp_directory_path();
    auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto png_path = temp_dir / ("brothumb_gen_" + nonce + ".png");
    auto bmp_path = temp_dir / ("brothumb_gen_" + nonce + ".bmp");
    auto ppm_path = temp_dir / ("brothumb_gen_" + nonce + ".ppm");
    auto cpp_path = temp_dir / ("brothumb_gen_" + nonce + ".cpp");

    struct Cleaner {
        std::filesystem::path a, b, c, d;
        ~Cleaner() {
            std::error_code ec;
            std::filesystem::remove(a, ec);
            std::filesystem::remove(b, ec);
            std::filesystem::remove(c, ec);
            std::filesystem::remove(d, ec);
        }
    } cleaner{png_path, bmp_path, ppm_path, cpp_path};

    // 1. Test Image Generator: PNG
    create_test_png(png_path, 200, 100);
    brothumb::ImageThumbnailGenerator img_gen;
    CHECK(img_gen.can_generate(png_path));

    brothumb::Image thumb_png;
    brothumb::Result res_png = img_gen.generate(png_path, 128, thumb_png);
    CHECK(res_png.ok);
    CHECK_EQ(thumb_png.width, 128);
    CHECK_EQ(thumb_png.height, 64); // Aspect ratio 2:1 preserved

    // 2. Test Image Generator: BMP
    create_test_bmp(bmp_path, 120, 80);
    CHECK(img_gen.can_generate(bmp_path));

    brothumb::Image thumb_bmp;
    brothumb::Result res_bmp = img_gen.generate(bmp_path, 60, thumb_bmp);
    CHECK(res_bmp.ok);
    CHECK_EQ(thumb_bmp.width, 60);
    CHECK_EQ(thumb_bmp.height, 40);

    // 3. Test Image Generator: PPM
    create_test_ppm(ppm_path, 80, 80);
    CHECK(img_gen.can_generate(ppm_path));

    brothumb::Image thumb_ppm;
    brothumb::Result res_ppm = img_gen.generate(ppm_path, 64, thumb_ppm);
    CHECK(res_ppm.ok);
    CHECK_EQ(thumb_ppm.width, 64);
    CHECK_EQ(thumb_ppm.height, 64);

    // 3b. JPEG (broimage), and routing by type rather than extension.
    auto jpg_path = temp_dir / ("brothumb_gen_" + nonce + ".jpg");
    auto noext_path = temp_dir / ("brothumb_gen_" + nonce + "_noext");
    auto fake_png = temp_dir / ("brothumb_gen_" + nonce + "_text.png");
    auto alpha_path = temp_dir / ("brothumb_gen_" + nonce + "_alpha.png");
    struct Cleaner2 {
        std::vector<std::filesystem::path> paths;
        ~Cleaner2() {
            std::error_code ec;
            for (const auto& p : paths) std::filesystem::remove(p, ec);
        }
    } cleaner2{{jpg_path, noext_path, fake_png, alpha_path}};
    {
        std::vector<uint8_t> rgb(90 * 30 * 3, 200), jpg;
        CHECK(broimage::encode_jpeg_memory(jpg, rgb.data(), 90, 30, 3, 90));
        write_bytes(jpg_path, jpg);
    }
    CHECK(img_gen.can_generate(jpg_path));
    brothumb::Image thumb_jpg;
    CHECK(img_gen.generate(jpg_path, 45, thumb_jpg).ok);
    CHECK_EQ(thumb_jpg.width, 45);
    CHECK_EQ(thumb_jpg.height, 15);

    create_test_png(noext_path, 40, 20); // a PNG with no extension is still a PNG
    brothumb::TextThumbnailGenerator text_check;
    CHECK(img_gen.can_generate(noext_path));
    CHECK(!text_check.can_generate(noext_path));
    {
        std::ofstream f(fake_png);
        f << "this is text that only claims to be a picture\n";
    }
    CHECK(!img_gen.can_generate(fake_png));
    CHECK(text_check.can_generate(fake_png));
    CHECK(img_gen.can_generate("x.bin", "image/x-ms-bmp")); // hint through the type database's aliases

    // TIFF (broimage's own decoder), upright by the orientation in its IFD0.
    auto tif_path = temp_dir / ("brothumb_gen_" + nonce + ".tif");
    cleaner2.paths.push_back(tif_path);
    create_test_tiff(tif_path, 60, 20);
    CHECK(img_gen.can_generate(tif_path));
    CHECK(img_gen.can_generate("x.bin", "image/tiff"));
    brothumb::Image thumb_tif;
    CHECK(img_gen.generate(tif_path, 30, thumb_tif).ok);
    CHECK_EQ(thumb_tif.width, 10);
    CHECK_EQ(thumb_tif.height, 30);
    CHECK(!thumb_tif.rgba.empty() && thumb_tif.rgba[0] == 128 && thumb_tif.rgba[3] == 255);
    CHECK(!img_gen.can_generate("x.png", "application/pdf"));

    // HEIF types are offered exactly where broimage can decode them: AVIF needs an AV1
    // decoder the host registers (none here), HEIC/HEIF the system's decoder.
    {
        auto types = img_gen.supported_mime_types();
        auto has = [&](const char* t) { return std::find(types.begin(), types.end(), t) != types.end(); };
        CHECK(!has("image/avif") && !broimage::can_decode("image/avif"));
        CHECK_EQ(has("image/heic"), broimage::can_decode("image/heic"));
        CHECK_EQ(img_gen.can_generate("x.bin", "image/heic"), broimage::can_decode("image/heic"));
        std::printf("brothumb: HEIC thumbnails %s\n", broimage::can_decode("image/heic") ? "available" : "unavailable");
    }

    // 3c. Downscaling filters in premultiplied alpha: the colour of fully transparent pixels
    // must not bleed into the opaque ones.
    {
        const int w = 64, h = 64;
        std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                uint8_t* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
                bool opaque = (x / 2 + y / 2) % 2 == 0;
                p[0] = opaque ? 0 : 255; // transparent pixels are red, opaque ones blue
                p[1] = 0;
                p[2] = opaque ? 255 : 0;
                p[3] = opaque ? 255 : 0;
            }
        }
        std::vector<uint8_t> png;
        broimage::encode_png_memory(png, rgba.data(), w, h, 4);
        write_bytes(alpha_path, png);
        brothumb::Image small;
        CHECK(img_gen.generate(alpha_path, 16, small).ok);
        CHECK_EQ(small.width, 16);
        int max_red = 0, min_alpha = 255, max_alpha = 0;
        for (size_t i = 0; i + 3 < small.rgba.size(); i += 4) {
            if (small.rgba[i + 3] > 0) max_red = std::max<int>(max_red, small.rgba[i]);
            min_alpha = std::min<int>(min_alpha, small.rgba[i + 3]);
            max_alpha = std::max<int>(max_alpha, small.rgba[i + 3]);
        }
        CHECK(max_red <= 8);                               // no red fringe
        CHECK(min_alpha >= 100 && max_alpha <= 160);       // area-averaged: half coverage everywhere
    }

    // 4. Test Custom Decoder Registration
    img_gen.register_decoder(".custom", [](const uint8_t*, size_t, brothumb::Image& out) {
        out.width = 16;
        out.height = 16;
        out.rgba.resize(16 * 16 * 4, 255);
        return brothumb::Result::success();
    });
    std::filesystem::path custom_path = temp_dir / "test.custom";
    CHECK(img_gen.can_generate(custom_path));

    // 5. Test Text Generator
    {
        std::ofstream f(cpp_path);
        f << "// Simple test program\n"
          << "#include <iostream>\n"
          << "\n"
          << "int main() {\n"
          << "    const char* message = \"Hello Brothumb!\";\n"
          << "    std::cout << message << std::endl;\n"
          << "    return 0;\n"
          << "}\n";
    }

    brothumb::TextThumbnailGenerator txt_gen;
    CHECK(txt_gen.can_generate(cpp_path));

    brothumb::Image thumb_txt;
    brothumb::Result res_txt = txt_gen.generate(cpp_path, 128, thumb_txt);
    CHECK(res_txt.ok);
    CHECK_EQ(thumb_txt.width, 128);
    CHECK_EQ(thumb_txt.height, 128);
    CHECK(!thumb_txt.empty());

    // 6. Test PDF Generator Interface
    brothumb::PdfThumbnailGenerator pdf_gen;
    std::printf("[test_generators] PDF available: %s (backend: %s)\n",
                brothumb::PdfThumbnailGenerator::is_available() ? "yes" : "no",
                brothumb::PdfThumbnailGenerator::backend_name().c_str());

    return bttest::finish("test_generators");
}
