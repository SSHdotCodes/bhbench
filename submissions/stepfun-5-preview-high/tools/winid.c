// ---------------------------------------------------------------------------
//  Window capture helper (development tool).
//  Prints the CoreGraphics window id of the first on-screen window whose
//  owner/title contains the given text, so we can screenshot ONLY the
//  simulation window:
//
//      screencapture -o -x -l $(tools/winid black-hole) /tmp/shot.png
//
//  Build:  clang -framework CoreGraphics -framework CoreFoundation tools/winid.c -o tools/winid
// ---------------------------------------------------------------------------
#include <CoreGraphics/CGWindow.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    const char *needle = argc > 1 ? argv[1] : "black-hole";
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
    if (!list) { fprintf(stderr, "cannot list windows\n"); return 2; }

    CFIndex n = CFArrayGetCount(list);
    for (CFIndex i = 0; i < n; ++i)
    {
        CFDictionaryRef d = CFArrayGetValueAtIndex(list, i);
        if (CFGetTypeID(d) != CFDictionaryGetTypeID()) continue;

        CFStringRef owner = (CFStringRef)CFDictionaryGetValue(d, kCGWindowOwnerName);
        CFStringRef title = (CFStringRef)CFDictionaryGetValue(d, kCGWindowName);
        char ob[256] = {0}, ti[256] = {0};
        if (owner) CFStringGetCString(owner, ob, sizeof(ob), kCFStringEncodingUTF8);
        if (title) CFStringGetCString(title, ti, sizeof(ti), kCFStringEncodingUTF8);
        if (!strstr(ob, needle) && !strstr(ti, needle)) continue;

        CFNumberRef idref = (CFNumberRef)CFDictionaryGetValue(d, kCGWindowNumber);
        CGWindowID wid = 0;
        if (idref) CFNumberGetValue(idref, kCFNumberIntType, &wid);
        if (wid) { printf("%u\n", (unsigned)wid); return 0; }
    }
    fprintf(stderr, "window '%s' not found\n", needle);
    return 1;
}
