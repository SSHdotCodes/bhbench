#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>
#include "BlackHoleRenderer.mm"

@interface MetalBlackHoleView : MTKView
@property (nonatomic, strong) BlackHoleRenderer *renderer;
@end

@implementation MetalBlackHoleView {
    NSPoint _lastMousePos;
    BOOL _isDragging;
    BOOL _isRightDrag;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)mouseDown:(NSEvent *)event {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return;
    }
    _isDragging = YES;
    _isRightDrag = NO;
    _lastMousePos = [event locationInWindow];
}

- (void)rightMouseDown:(NSEvent *)event {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return;
    }
    _isDragging = YES;
    _isRightDrag = YES;
    _lastMousePos = [event locationInWindow];
}

- (void)mouseUp:(NSEvent *)event {
    _isDragging = NO;
}

- (void)rightMouseUp:(NSEvent *)event {
    _isDragging = NO;
}

- (void)mouseDragged:(NSEvent *)event {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return;
    }
    if (_isDragging && self.renderer) {
        NSPoint currentPos = [event locationInWindow];
        float dx = currentPos.x - _lastMousePos.x;
        float dy = currentPos.y - _lastMousePos.y;
        _lastMousePos = currentPos;
        [self.renderer handleMouseDragWithDeltaX:dx deltaY:dy isRightButton:_isRightDrag];
    }
}

- (void)rightMouseDragged:(NSEvent *)event {
    [self mouseDragged:event];
}

- (void)scrollWheel:(NSEvent *)event {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return;
    }
    if (self.renderer) {
        [self.renderer handleScrollWithDeltaY:event.scrollingDeltaY];
    }
}

@end

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property (nonatomic, strong) NSWindow *window;
@property (nonatomic, strong) MetalBlackHoleView *metalView;
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    NSRect frame = NSMakeRect(100, 100, 1280, 800);
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;
    
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    self.window.title = @"General Relativistic Kerr Black Hole Simulation (Realtime Metal)";
    self.window.delegate = self;
    self.window.backgroundColor = [NSColor blackColor];
    
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        NSLog(@"Metal is not supported on this device");
        [NSApp terminate:nil];
        return;
    }
    
    self.metalView = [[MetalBlackHoleView alloc] initWithFrame:frame device:device];
    self.metalView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    self.metalView.framebufferOnly = NO; // Allows compute kernel writes
    self.metalView.preferredFramesPerSecond = 60;
    self.metalView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    
    BlackHoleRenderer *renderer = [[BlackHoleRenderer alloc] initWithMetalKitView:self.metalView];
    self.metalView.renderer = renderer;
    self.metalView.delegate = renderer;
    
    [self.window setContentView:self.metalView];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}

@end

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        
        AppDelegate *delegate = [[AppDelegate alloc] init];
        [app setDelegate:delegate];
        [app run];
    }
    return 0;
}
