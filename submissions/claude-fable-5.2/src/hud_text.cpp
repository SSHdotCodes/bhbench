// =============================================================================
//  hud_text.cpp — rasterises the on-screen read-out with CoreText (macOS).
//  Plain C API, no Objective-C.  On other platforms the HUD is simply absent
//  and the same information goes to the window title.
// =============================================================================
#include "hud_text.hpp"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include <cmath>

namespace hud {

static CTLineRef makeLine(const std::string& text, CTFontRef font, CGColorRef color) {
    CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault, text.c_str(), kCFStringEncodingUTF8);
    if (!str) str = CFStringCreateWithCString(kCFAllocatorDefault, "?", kCFStringEncodingUTF8);
    const void* keys[2] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
    const void* vals[2] = {font, color};
    CFDictionaryRef attrs = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef as = CFAttributedStringCreate(kCFAllocatorDefault, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(as);
    CFRelease(as);
    CFRelease(attrs);
    CFRelease(str);
    return line;
}

bool render(const std::vector<Line>& lines, float pointSize, float scale, float backing, Bitmap& out) {
    if (lines.empty()) return false;
    CGFloat size = pointSize * scale;
    CTFontRef regular = CTFontCreateWithName(CFSTR("Menlo-Regular"), size, nullptr);
    CTFontRef bold = CTFontCreateWithName(CFSTR("Menlo-Bold"), size, nullptr);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();

    const CGFloat palette[4][4] = {
        {0.86, 0.90, 0.96, 1.0},      // Normal
        {1.00, 0.80, 0.45, 1.0},      // Title
        {0.55, 0.62, 0.72, 1.0},      // Dim
        {0.45, 0.85, 1.00, 1.0},      // Accent
    };

    CGFloat ascent = CTFontGetAscent(regular), descent = CTFontGetDescent(regular);
    CGFloat lineH = std::ceil((ascent + descent) * 1.28);
    CGFloat pad = std::ceil(8.0 * scale);

    std::vector<CTLineRef> ct(lines.size());
    CGFloat maxW = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        CGColorRef col = CGColorCreate(cs, palette[int(lines[i].style) & 3]);
        ct[i] = makeLine(lines[i].text, lines[i].style == Style::Title ? bold : regular, col);
        CGColorRelease(col);
        maxW = std::fmax(maxW, CTLineGetTypographicBounds(ct[i], nullptr, nullptr, nullptr));
    }

    out.w = int(std::ceil(maxW + 2 * pad));
    out.h = int(std::ceil(lineH * lines.size() + 2 * pad));
    out.rgba.assign(size_t(out.w) * out.h * 4, 0);

    CGContextRef ctx = CGBitmapContextCreate(out.rgba.data(), out.w, out.h, 8, size_t(out.w) * 4, cs,
                                             uint32_t(kCGImageAlphaPremultipliedLast) | uint32_t(kCGBitmapByteOrder32Big));
    bool ok = ctx != nullptr;
    if (ok) {
        // faint rounded backing panel; legibility comes from the text shadow
        CGContextSetRGBFillColor(ctx, 0.0, 0.0, 0.0, backing);
        CGRect rect = CGRectMake(0, 0, out.w, out.h);
        CGPathRef path = CGPathCreateWithRoundedRect(rect, 7 * scale, 7 * scale, nullptr);
        CGContextAddPath(ctx, path);
        CGContextFillPath(ctx);
        CGPathRelease(path);

        const CGFloat black[4] = {0, 0, 0, 0.95};
        CGColorRef shadow = CGColorCreate(cs, black);
        CGContextSetShadowWithColor(ctx, CGSizeMake(0, -1.0 * scale), 2.5 * scale, shadow);
        CGColorRelease(shadow);

        CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
        for (size_t i = 0; i < ct.size(); ++i) {
            CGFloat y = out.h - pad - ascent - lineH * i - (lineH - ascent - descent) * 0.5;
            CGContextSetTextPosition(ctx, pad, y);
            CTLineDraw(ct[i], ctx);
        }
        CGContextRelease(ctx);
    }
    for (CTLineRef l : ct) CFRelease(l);
    CGColorSpaceRelease(cs);
    CFRelease(regular);
    CFRelease(bold);
    return ok;
}

}  // namespace hud

#else

namespace hud {
bool render(const std::vector<Line>&, float, float, float, Bitmap&) { return false; }
}  // namespace hud

#endif
