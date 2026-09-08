// main.mm — Cocoa entry point: window, Metal view, input routing, HUD overlay.
#import "renderer.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>

@interface BHView : MTKView
@property (nonatomic, weak) BHRenderer *renderer;
@end
@implementation BHView
- (BOOL)acceptsFirstResponder { return YES; }
- (void)keyDown:(NSEvent *)event { [self.renderer keyDown:event]; }
- (void)mouseDragged:(NSEvent *)event { [self.renderer mouseDrag:event.deltaX dy:event.deltaY secondary:NO]; }
- (void)rightMouseDragged:(NSEvent *)event { [self.renderer mouseDrag:event.deltaX dy:event.deltaY secondary:YES]; }
- (void)scrollWheel:(NSEvent *)event { [self.renderer scroll:event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 0.25 : 3.0)]; }
@end

@interface BHAppDelegate : NSObject <NSApplicationDelegate>
@end
@implementation BHAppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender { return YES; }
@end

static void usage() {
    std::printf("usage: blackhole [--spin a] [--inc deg] [--dist r] [--scale s] [--t0 K] [--grid 0-3] [--no-bloom] [--no-disk]\n"
                "                 [--width W] [--height H] [--frames N --screenshot out.png] [--shaders dir]\n");
}

int main(int argc, char **argv) {
    @autoreleasepool {
        BHOptions opts;
        int W = 1280, H = 800;
        for (int i = 1; i < argc; ++i) {
            auto next = [&](double &v) { if (i + 1 < argc) v = std::atof(argv[++i]); };
            if (!std::strcmp(argv[i], "--spin")) next(opts.spin);
            else if (!std::strcmp(argv[i], "--inc")) next(opts.inclinationDeg);
            else if (!std::strcmp(argv[i], "--dist")) next(opts.camR);
            else if (!std::strcmp(argv[i], "--scale")) next(opts.renderScale);
            else if (!std::strcmp(argv[i], "--t0")) next(opts.tScale);
            else if (!std::strcmp(argv[i], "--grid") && i + 1 < argc) opts.gridMode = std::atoi(argv[++i]);
            else if (!std::strcmp(argv[i], "--no-bloom")) opts.bloom = false;
            else if (!std::strcmp(argv[i], "--no-disk")) opts.disk = false;
            else if (!std::strcmp(argv[i], "--width") && i + 1 < argc) W = std::atoi(argv[++i]);
            else if (!std::strcmp(argv[i], "--height") && i + 1 < argc) H = std::atoi(argv[++i]);
            else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) opts.frames = std::atoi(argv[++i]);
            else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) opts.screenshotPath = [NSString stringWithUTF8String:argv[++i]];
            else if (!std::strcmp(argv[i], "--dump") && i + 1 < argc) opts.dumpPath = [NSString stringWithUTF8String:argv[++i]];
            else if (!std::strcmp(argv[i], "--shaders") && i + 1 < argc) opts.shaderDir = [NSString stringWithUTF8String:argv[++i]];
            else { usage(); return std::strcmp(argv[i], "--help") ? 1 : 0; }
        }
        if (!opts.shaderDir) {
            // default: <executable dir>/src/shaders
            NSString *exe = [[NSBundle mainBundle] executablePath];
            opts.shaderDir = [[exe stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"src/shaders"];
        }

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        BHAppDelegate *del = [BHAppDelegate new];
        [NSApp setDelegate:del];

        NSMenu *menubar = [NSMenu new]; NSMenuItem *appItem = [NSMenuItem new]; [menubar addItem:appItem];
        NSMenu *appMenu = [NSMenu new];
        [appMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"]];
        [appItem setSubmenu:appMenu]; [NSApp setMainMenu:menubar];

        NSScreen *screen = [NSScreen mainScreen];
        if (screen) {   // keep the window inside the main screen
            const NSRect vf = screen.visibleFrame;
            W = std::min(W, (int)vf.size.width - 40); H = std::min(H, (int)vf.size.height - 60);
        }
        NSRect frame = NSMakeRect(0, 0, W, H);
        NSWindow *win = [[NSWindow alloc] initWithContentRect:frame
                                                    styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable)
                                                      backing:NSBackingStoreBuffered defer:NO];
        win.title = @"Kerr black hole — ray-traced accretion disk & spacetime embedding (Metal)";
        [win center];

        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) { std::fprintf(stderr, "No Metal device\n"); return 1; }
        BHView *view = [[BHView alloc] initWithFrame:frame device:dev];
        view.colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
        view.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
        view.framebufferOnly = NO;
        view.preferredFramesPerSecond = 120;
        view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

        NSTextField *hud = [[NSTextField alloc] initWithFrame:NSMakeRect(10, H - 90, W - 20, 80)];
        hud.editable = NO; hud.bezeled = NO; hud.drawsBackground = YES; hud.selectable = NO;
        hud.backgroundColor = [NSColor colorWithWhite:0 alpha:0.35];
        hud.textColor = [NSColor colorWithWhite:0.92 alpha:1];
        hud.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
        hud.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
        hud.lineBreakMode = NSLineBreakByWordWrapping; hud.maximumNumberOfLines = 0;
        hud.stringValue = @"initialising…";
        [view addSubview:hud];

        BHRenderer *renderer = [[BHRenderer alloc] initWithView:view hud:hud options:opts];
        view.renderer = renderer;
        view.delegate = renderer;
        win.contentView = view;
        [win makeFirstResponder:view];
        [win makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        std::printf("Kerr black hole simulation — window open. Press H in the window for controls, Q to quit.\n");
        [NSApp run];
    }
    return 0;
}
