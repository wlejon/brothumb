#if defined(__APPLE__)

#include "brothumb/native.h"
#import <QuickLook/QuickLook.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#import <Foundation/Foundation.h>
#include <algorithm>

namespace brothumb {

bool NativeThumbnailExtractor::is_supported() {
    return true;
}

std::string NativeThumbnailExtractor::provider_name() {
    return "macOS Quick Look (QLThumbnailImageCreate)";
}

PlatformCapabilities NativeThumbnailExtractor::capabilities() {
    PlatformCapabilities caps;
    caps.has_xdg_cache = true;
    caps.has_windows_shell = false;
    caps.has_macos_quicklook = true;
    caps.has_pdf_rendering = true;
    caps.pdf_backend = "PDFKit";
    caps.supported_extensions = {
        ".png", ".jpg", ".jpeg", ".tiff", ".bmp", ".gif", ".heic", ".heif", ".avif", ".webp",
        ".pdf", ".mp4", ".mov", ".m4v",
        ".txt", ".md", ".cpp", ".h"
    };
    return caps;
}

Result NativeThumbnailExtractor::extract(const std::filesystem::path& path,
                                         int32_t target_size,
                                         Image& out_image) {
    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        if (!nsPath) {
            return Result::failure("Invalid path encoding for macOS");
        }

        NSURL* fileUrl = [NSURL fileURLWithPath:nsPath];
        if (!fileUrl) {
            return Result::failure("Failed to create NSURL for path: " + path.string());
        }

        CGSize size = CGSizeMake(target_size, target_size);
        NSDictionary* options = @{
            (NSString*)kQLThumbnailOptionIconModeKey: @NO,
            (NSString*)kQLThumbnailOptionScaleFactorKey: @1.0
        };

        CGImageRef cgImage = QLThumbnailImageCreate(kCFAllocatorDefault,
                                                    (__bridge CFURLRef)fileUrl,
                                                    size,
                                                    (__bridge CFDictionaryRef)options);

        // Fallback to ImageIO if QuickLook did not return an image
        if (!cgImage) {
            CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)fileUrl, nullptr);
            if (src) {
                NSDictionary* thumbOpts = @{
                    (NSString*)kCGImageSourceCreateThumbnailWithTransform: @YES,
                    (NSString*)kCGImageSourceCreateThumbnailFromImageAlways: @YES,
                    (NSString*)kCGImageSourceThumbnailMaxPixelSize: @(target_size)
                };
                cgImage = CGImageSourceCreateThumbnailAtIndex(src, 0, (__bridge CFDictionaryRef)thumbOpts);
                CFRelease(src);
            }
        }

        if (!cgImage) {
            return Result::failure("QuickLook / ImageIO could not generate thumbnail for: " + path.string());
        }

        size_t width = CGImageGetWidth(cgImage);
        size_t height = CGImageGetHeight(cgImage);

        if (width == 0 || height == 0) {
            CGImageRelease(cgImage);
            return Result::failure("Native thumbnail image has 0 dimension");
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
            CGImageRelease(cgImage);
            return Result::failure("Failed to create CGBitmapContext for native thumbnail");
        }

        CGContextDrawImage(context, CGRectMake(0, 0, width, height), cgImage);
        CGContextRelease(context);
        CGImageRelease(cgImage);

        // Un-premultiply alpha to straight RGBA8
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
