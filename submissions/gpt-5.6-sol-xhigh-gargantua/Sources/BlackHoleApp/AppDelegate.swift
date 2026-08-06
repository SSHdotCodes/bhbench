import AppKit
import MetalKit

final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    private let options: AppOptions
    private var window: NSWindow?
    private var renderer: Renderer?
    private var overlay: OverlayController?
    private var benchmarkTimer: Timer?

    init(options: AppOptions) {
        self.options = options
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        guard let device = MTLCreateSystemDefaultDevice() else {
            presentFatalError(RendererError.noMetalDevice)
            return
        }

        let configuration = options.configuration
        let view = BlackHoleView(
            frame: NSRect(x: 0, y: 0, width: 1_280, height: 720),
            device: device
        )
        view.drawableSize = CGSize(width: configuration.width, height: configuration.height)
        let container = RenderContainerView(metalView: view)

        do {
            let renderer = try Renderer(
                view: view,
                configuration: configuration,
                overlayVisible: options.showOverlay
            )
            self.renderer = renderer
            let overlay = OverlayController(
                view: container.overlayView,
                configuration: configuration,
                snapshot: { [weak renderer] in renderer?.performanceSnapshot ?? PerformanceSnapshot(fps: 0, averageGPUms: 0, p95GPUms: 0, frameCount: 0) },
                renderScale: { [weak renderer] in renderer?.renderScale ?? 0 },
                headroom: { [weak renderer] in renderer?.edrHeadroom ?? 1 }
            )
            overlay.setVisible(options.showOverlay)
            renderer.overlayVisibilityChanged = { [weak overlay] visible in overlay?.setVisible(visible) }
            self.overlay = overlay
        } catch {
            presentFatalError(error)
            return
        }

        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1_280, height: 720),
            styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )
        window.title = "Gargantua"
        window.titleVisibility = .hidden
        window.titlebarAppearsTransparent = true
        window.backgroundColor = .black
        window.isOpaque = true
        window.minSize = NSSize(width: 800, height: 450)
        window.contentView = container
        window.delegate = self
        window.center()
        window.makeKeyAndOrderFront(nil)
        window.makeFirstResponder(view)
        self.window = window

        NSApplication.shared.activate(ignoringOtherApps: true)
        if options.fullscreen {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { [weak window] in
                window?.toggleFullScreen(nil)
            }
        }
        scheduleBenchmarkIfNeeded()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    private func scheduleBenchmarkIfNeeded() {
        guard let seconds = options.configuration.benchmarkSeconds else { return }
        benchmarkTimer = Timer.scheduledTimer(withTimeInterval: max(2, seconds), repeats: false) { [weak self] _ in
            self?.finishBenchmark()
        }
    }

    private func finishBenchmark() {
        guard let renderer else { return }
        let snapshot = renderer.performanceSnapshot
        let trace = renderer.traceResolution
        let target = Double(options.configuration.targetFPS)
        let frameBudget = 1_000 / target
        let report: [String: Any] = [
            "device": rendererDeviceName(),
            "output_resolution": "\(options.configuration.width)x\(options.configuration.height)",
            "trace_resolution": "\(Int(trace.width))x\(Int(trace.height))",
            "render_scale": renderer.renderScale,
            "average_fps": snapshot.fps,
            "average_gpu_ms": snapshot.averageGPUms,
            "p95_gpu_ms": snapshot.p95GPUms,
            "target_fps": options.configuration.targetFPS,
            "stable_target": snapshot.fps >= target * 0.97 && snapshot.p95GPUms <= frameBudget,
            "edr_headroom": renderer.edrHeadroom,
        ]
        if let data = try? JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys]),
           let json = String(data: data, encoding: .utf8) {
            print("BLACKHOLE_BENCHMARK_BEGIN")
            print(json)
            print("BLACKHOLE_BENCHMARK_END")
            fflush(stdout)
        }
        NSApplication.shared.terminate(nil)
    }

    private func rendererDeviceName() -> String {
        MTLCreateSystemDefaultDevice()?.name ?? "Unknown Metal GPU"
    }

    private func presentFatalError(_ error: Error) {
        let alert = NSAlert(error: error)
        alert.runModal()
        NSApplication.shared.terminate(nil)
    }
}
