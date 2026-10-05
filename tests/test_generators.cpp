#include "brothumb/generator.h"
#include "brothumb/metadata.h"
#include "check.h"

#include <broimage/encode.h>

#include <algorithm>
#include <chrono>
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
    CHECK(!img_gen.can_generate("x.png", "application/pdf"));

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
