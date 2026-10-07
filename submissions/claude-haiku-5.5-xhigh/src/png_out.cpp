#include "png_out.h"

#include <ImageIO/ImageIO.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstring>
#include <vector>

bool write_png_rgb8(const char* path, const uint8_t* rgb, int width, int height) {
    std::vector<uint8_t> rgbx(size_t(width) * size_t(height) * 4);
    for (size_t i = 0, n = size_t(width) * size_t(height); i < n; ++i) {
        rgbx[4 * i + 0] = rgb[3 * i + 0];
        rgbx[4 * i + 1] = rgb[3 * i + 1];
        rgbx[4 * i + 2] = rgb[3 * i + 2];
        rgbx[4 * i + 3] = 255;
    }
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, rgbx.data(), CFIndex(rgbx.size()));
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef image = CGImageCreate(size_t(width), size_t(height), 8, 32, size_t(width) * 4, space,
                                     kCGImageAlphaNoneSkipLast | kCGBitmapByteOrderDefault, provider, nullptr, false,
                                     kCGRenderingIntentDefault);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(path),
                                                           CFIndex(std::strlen(path)), false);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
    bool ok = false;
    if (dest && image) {
        CGImageDestinationAddImage(dest, image, nullptr);
        ok = CGImageDestinationFinalize(dest);
    }
    if (dest) CFRelease(dest);
    if (url) CFRelease(url);
    if (image) CGImageRelease(image);
    if (space) CGColorSpaceRelease(space);
    if (provider) CGDataProviderRelease(provider);
    if (data) CFRelease(data);
    return ok;
}
