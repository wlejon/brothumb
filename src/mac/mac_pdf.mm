#if defined(__APPLE__)

#include "brothumb/generator.h"
#import <PDFKit/PDFKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#include <algorithm>
#include <cctype>

namespace brothumb {

PdfThumbnailGenerator::PdfThumbnailGenerator() {}

bool PdfThumbnailGenerator::is_available() {
    return true;
}

std::string PdfThumbnailGenerator::backend_name() {
    return "PDFKit";
}

bool PdfThumbnailGenerator::can_generate(const std::filesystem::path& path,
                                        const std::string& mime_hint) const {
    if (mime_hint == "application/pdf") return true;
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".pdf";
}

Result PdfThumbnailGenerator::generate(const std::filesystem::path& path,
                                       int32_t target_size,
                                       Image& out_image) {
    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        if (!nsPath) {
            return Result::failure("Invalid path encoding for PDF");
        }

        NSURL* fileUrl = [NSURL fileURLWithPath:nsPath];
        if (!fileUrl) {
            return Result::failure("Failed to create NSURL for: " + path.string());
        }

        PDFDocument* doc = [[PDFDocument alloc] initWithURL:fileUrl];
        if (!doc || [doc pageCount] == 0) {
            return Result::failure("Failed to open PDF document or PDF has 0 pages");
        }

        PDFPage* page = [doc pageAtIndex:0];
        if (!page) {
            return Result::failure("Failed to get first page from PDF");
        }

        NSImage* nsImg = [page thumbnailOfSize:NSMakeSize(target_size, target_size)
                                          forBox:kPDFDisplayBoxCropBox];
        if (!nsImg) {
            return Result::failure("PDFKit failed to render page thumbnail");
        }

        NSRect imageRect = NSMakeRect(0, 0, nsImg.size.width, nsImg.size.height);
        CGImageRef cgImage = [nsImg CGImageForProposedRect:&imageRect context:nil hints:nil];
        if (!cgImage) {
            return Result::failure("Failed to obtain CGImageRef from PDFKit thumbnail");
        }

        size_t width = CGImageGetWidth(cgImage);
        size_t height = CGImageGetHeight(cgImage);
        if (width == 0 || height == 0) {
            return Result::failure("Rendered PDF thumbnail has 0 dimension");
        }

        out_image.width = static_cast<int32_t>(width);
        out_image.height = static_cast<int32_t>(height);
        out_image.rgba.resize(width * height * 4);

        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        uint32_t bitmapInfo = static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) |
                              static_cast<uint32_t>(kCGBitmapByteOrder32Big);
        CGContextRef context = CGBitmapContextCreate(out_image.rgba.data(),
                                                     width, height, 8, width * 4,
                                                     colorSpace,
                                                     bitmapInfo);
        CGColorSpaceRelease(colorSpace);

        if (!context) {
            return Result::failure("Failed to create CGBitmapContext for PDF thumbnail");
        }

        CGContextDrawImage(context, CGRectMake(0, 0, width, height), cgImage);
        CGContextRelease(context);

        // Un-premultiply
        for (size_t i = 0; i < out_image.rgba.size(); i += 4) {
            uint8_t a = out_image.rgba[i + 3];
            if (a > 0 && a < 255) {
                out_image.rgba[i + 0] = static_cast<uint8_t>(std::min(255, (out_image.rgba[i + 0] * 255 + a / 2) / a));
                out_image.rgba[i + 1] = static_cast<uint8_t>(std::min(255, (out_image.rgba[i + 1] * 255 + a / 2) / a));
                out_image.rgba[i + 2] = static_cast<uint8_t>(std::min(255, (out_image.rgba[i + 2] * 255 + a / 2) / a));
            }
        }

        return Result::success();
    }
}

}  // namespace brothumb

#endif  // __APPLE__
