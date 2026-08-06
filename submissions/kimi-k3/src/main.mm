// main.mm — Black hole geodesic ray tracer: window, input, realtime loop.
//
// Controls:
//   drag        orbit camera          scroll      zoom
//   G           toggle spacetime grid D           toggle accretion disk
//   K           toggle sky            B           toggle relativistic beaming
//   N           cycle background (ray-traced / plain black)
//   Space       pause/resume auto-orbit
//   1 / 2 / 3   quality presets (integration steps / resolution scale)
//   P           save screenshot.png   R           reset camera
//   Esc         quit
//
// Environment overrides (for testing): BH_GRID, BH_DISK, BH_SKY, BH_BEAM,
//   BH_YAW, BH_PITCH, BH_DIST, BH_STEPS, BH_SCALE, BH_BG, BH_EXPOSURE,
//   BH_SHOT_FRAMES (save a screenshot after N frames), BH_SHOT_PATH,
//   BH_EXIT_AFTER_SHOT.

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import "Renderer.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>

// ---------------------------------------------------------------------------

struct SharedState {
    std::mutex m;
    SimSnapshot snap;
    bool autoRotate = true;
    std::atomic<bool> running{true};
    std::atomic<int> drawableW{1280}, drawableH{800};
    int shotFrames = -1;             // auto-screenshot after N frames (-1 = off)
    std::string shotPath = "screenshot.png";
    bool exitAfterShot = false;
};

static float envFloat(const char* name, float def) {
    const char* v = getenv(name);
    return v ? atof(v) : def;
}
static bool envBool(const char* name, bool def) {
    const char* v = getenv(name);
    return v ? (atoi(v) != 0) : def;
}

// ---------------------------------------------------------------------------

@interface BHView : NSView
@property(nonatomic, assign) SharedState* state;
@end

@implementation BHView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return YES; }

- (CALayer*)makeBackingLayer {
    CAMetalLayer* l = [CAMetalLayer layer];
    l.contentsScale = [self.window backingScaleFactor];
    return l;
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    [self updateLayerGeometry];
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    [self updateLayerGeometry];
}

- (void)updateLayerGeometry {
    CAMetalLayer* l = (CAMetalLayer*)self.layer;
    if (!l || !self.window) return;
    l.frame = self.bounds;
    l.contentsScale = [self.window backingScaleFactor];
    CGSize px = [self convertSizeToBacking:self.bounds.size];
    l.drawableSize = px;
    if (_state) {
        _state->drawableW = (int)px.width;
        _state->drawableH = (int)px.height;
    }
}

- (void)mouseDragged:(NSEvent*)e {
    if (!_state) return;
    std::lock_guard<std::mutex> lk(_state->m);
    _state->snap.yaw += (float)e.deltaX * 0.005f;
    _state->snap.pitch -= (float)e.deltaY * 0.005f;
    _state->snap.pitch = fmaxf(-1.45f, fminf(1.45f, _state->snap.pitch));
}

- (void)scrollWheel:(NSEvent*)e {
    if (!_state) return;
    std::lock_guard<std::mutex> lk(_state->m);
    _state->snap.dist *= expf(-(float)e.scrollingDeltaY * 0.01f);
    _state->snap.dist = fmaxf(4.0f, fminf(60.0f, _state->snap.dist));
}

- (void)keyDown:(NSEvent*)e {
    if (!_state) return;
    NSString* s = [e.charactersIgnoringModifiers lowercaseString];
    if (s.length == 0) return;
    unichar c = [s characterAtIndex:0];
    if (c == 27) { [NSApp terminate:nil]; return; }   // Esc
    std::lock_guard<std::mutex> lk(_state->m);
    switch (c) {
        case 'g': _state->snap.grid = !_state->snap.grid; break;
        case 'd': _state->snap.disk = !_state->snap.disk; break;
        case 'k': _state->snap.sky = !_state->snap.sky; break;
        case 'b': _state->snap.beaming = !_state->snap.beaming; break;
        case 'n': _state->snap.bgRaytrace = !_state->snap.bgRaytrace; break;
        case ' ': _state->autoRotate = !_state->autoRotate; break;
        case '1': _state->snap.maxSteps = 800;  _state->snap.renderScale = 0.60f; break;
        case '2': _state->snap.maxSteps = 1500; _state->snap.renderScale = 0.75f; break;
        case '3': _state->snap.maxSteps = 3000; _state->snap.renderScale = 1.00f; break;
        case 'p': _state->snap.shot = true; break;
        case 'r':
            _state->snap.yaw = 0.0f; _state->snap.pitch = 0.30f; _state->snap.dist = 18.0f;
            break;
        default: break;
    }
}
@end

// ---------------------------------------------------------------------------

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) BHView* view;
@property(nonatomic, strong) Renderer* renderer;
@property(nonatomic, assign) SharedState* state;
@property(nonatomic, assign) std::thread* renderThread;
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)note {
    (void)note;
    _state = new SharedState();

    // environment overrides (used for automated testing)
    _state->snap.grid      = envBool("BH_GRID", true);
    _state->snap.disk      = envBool("BH_DISK", true);
    _state->snap.sky       = envBool("BH_SKY", true);
    _state->snap.beaming   = envBool("BH_BEAM", true);
    _state->snap.bgRaytrace = envBool("BH_BG", true);
    _state->snap.yaw       = envFloat("BH_YAW", 0.0f);
    _state->snap.pitch     = envFloat("BH_PITCH", 0.30f);
    _state->snap.dist      = envFloat("BH_DIST", 18.0f);
    _state->snap.maxSteps  = (int)envFloat("BH_STEPS", 1500);
    _state->snap.renderScale = envFloat("BH_SCALE", 0.75f);
    _state->snap.exposure  = envFloat("BH_EXPOSURE", 1.0f);
    _state->shotFrames     = (int)envFloat("BH_SHOT_FRAMES", -1);
    if (getenv("BH_SHOT_PATH")) _state->shotPath = getenv("BH_SHOT_PATH");
    _state->exitAfterShot  = envBool("BH_EXIT_AFTER_SHOT", false);

    NSRect frame = NSMakeRect(0, 0, 1280, 800);
    _window = [[NSWindow alloc]
        initWithContentRect:frame
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    _window.title = @"Black Hole — Schwarzschild geodesic ray tracer";
    [_window center];

    _view = [[BHView alloc] initWithFrame:frame];
    _view.state = _state;
    _view.wantsLayer = YES;
    _window.contentView = _view;
    [_window makeKeyAndOrderFront:nil];
    [_window makeFirstResponder:_view];
    [NSApp activateIgnoringOtherApps:YES];

    CAMetalLayer* layer = (CAMetalLayer*)_view.layer;
    _renderer = [[Renderer alloc] initWithLayer:layer error:nil];
    if (!_renderer) {
        NSLog(@"Failed to initialize renderer");
        [NSApp terminate:nil];
        return;
    }
    [_view updateLayerGeometry];

    __weak AppDelegate* weakSelf = self;
    _renderThread = new std::thread([weakSelf]() {
        AppDelegate* strongSelf = weakSelf;
        if (!strongSelf) return;
        [strongSelf renderLoop];
    });
}

- (void)renderLoop {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    auto fpsStart = clock::now();
    int frames = 0, totalFrames = 0;

    while (_state->running.load()) {
        @autoreleasepool {
            auto now = clock::now();
            float dt = std::chrono::duration<float>(now - last).count();
            last = now;

            SimSnapshot snap;
            {
                std::lock_guard<std::mutex> lk(_state->m);
                if (_state->autoRotate) _state->snap.yaw += dt * 0.05f;
                snap = _state->snap;
                _state->snap.shot = false;      // consume
            }
            if (_state->shotFrames >= 0 && totalFrames >= _state->shotFrames) {
                snap.shot = true;
                snap.shotPath = _state->shotPath;
                _state->shotFrames = -1;        // once
                if (_state->exitAfterShot) _state->running = false;
            }

            float t = std::chrono::duration<float>(now.time_since_epoch()).count();
            CGSize sz = CGSizeMake(_state->drawableW.load(), _state->drawableH.load());
            [_renderer drawSnapshot:snap time:t drawableSize:sz];

            frames++; totalFrames++;
            double elapsed = std::chrono::duration<double>(now - fpsStart).count();
            if (elapsed >= 1.0) {
                int fps = (int)(frames / elapsed);
                frames = 0; fpsStart = now;
                static bool logFps = getenv("BH_LOG_FPS") != nullptr;
                if (logFps) NSLog(@"FPS: %d (steps=%d, scale=%.2f)", fps, snap.maxSteps, snap.renderScale);
                NSString* title = [NSString stringWithFormat:
                    @"Black Hole — %d fps — grid:%@ disk:%@ beam:%@ steps:%d scale:%.2f",
                    fps, snap.grid ? @"on" : @"off", snap.disk ? @"on" : @"off",
                    snap.beaming ? @"on" : @"off", snap.maxSteps, snap.renderScale];
                dispatch_async(dispatch_get_main_queue(), ^{
                    self.window.title = title;
                });
            }
        }
    }

    if (_state->exitAfterShot) {
        dispatch_async(dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
    }
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
    (void)app;
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)note {
    (void)note;
    _state->running = false;
    if (_renderThread) { _renderThread->join(); delete _renderThread; _renderThread = nullptr; }
}

@end

// ---------------------------------------------------------------------------

int main(int argc, const char* argv[]) {
    (void)argc; (void)argv;
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate* delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
