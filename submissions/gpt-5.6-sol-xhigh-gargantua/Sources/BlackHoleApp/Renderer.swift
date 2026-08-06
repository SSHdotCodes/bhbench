import AppKit
import BlackHoleCore
import Metal
import MetalKit
import QuartzCore

private struct ShaderUniforms {
    var traceSize: SIMD2<UInt32>
    var outputSize: SIMD2<UInt32>
    var camera: SIMD4<Float>
    var image: SIMD4<Float>
    var sampling: SIMD4<UInt32>
    var disk: SIMD4<Float>
}

enum RendererError: LocalizedError {
    case noMetalDevice
    case noCommandQueue
    case missingShader
    case missingFunction(String)
    case textureAllocation

    var errorDescription: String? {
        switch self {
        case .noMetalDevice: return "This Mac does not expose a Metal GPU."
        case .noCommandQueue: return "Metal could not create a command queue."
        case .missingShader: return "BlackHole.metal is missing from the application resources."
        case .missingFunction(let name): return "The Metal shader function \(name) is missing."
        case .textureAllocation: return "Metal could not allocate the full-resolution HDR render targets."
        }
    }
}

final class Renderer: NSObject, MTKViewDelegate, BlackHoleInteractionDelegate {
    private let view: BlackHoleView
    private let device: MTLDevice
    private let commandQueue: MTLCommandQueue
    private let lensMapPipeline: MTLComputePipelineState
    private let shadePipeline: MTLComputePipelineState
    private let presentPipeline: MTLComputePipelineState
    private let monitor = PerformanceMonitor()
    private let inFlightSemaphore = DispatchSemaphore(value: 3)
    private let mapPhaseCount: UInt32 = 8
    private var configuration: RenderConfiguration
    private var lensMap: MTLTexture!
    private var radianceTexture: MTLTexture!
    private var lensMapInitialized = false
    private var pendingMapPhases: UInt32 = 0
    private var nextMapPhase: UInt32 = 0
    private var frameIndex: UInt32 = 0
    private var cameraInclination: Float = 1.558
    private let cameraRadius: Float = 24
    private var cameraFOV: Float = 46 * .pi / 180
    private var cameraAzimuth: Float = 0
    private var exposure: Float
    private var animationStarted = CACurrentMediaTime()
    private var frozenAnimationTime: Float = 0
    private var isAnimating = true
    private var overlayVisible: Bool
    private var cachedEDRHeadroom = 1.0
    var overlayVisibilityChanged: ((Bool) -> Void)?

    init(view: BlackHoleView, configuration: RenderConfiguration, overlayVisible: Bool) throws {
        guard let device = view.device ?? MTLCreateSystemDefaultDevice() else { throw RendererError.noMetalDevice }
        guard let commandQueue = device.makeCommandQueue() else { throw RendererError.noCommandQueue }
        guard let shaderURL = Bundle.module.url(
            forResource: "BlackHole",
            withExtension: "metal",
            subdirectory: "Shaders"
        ) else { throw RendererError.missingShader }

        let shaderSource = try String(contentsOf: shaderURL, encoding: .utf8)
        let compileOptions = MTLCompileOptions()
        compileOptions.fastMathEnabled = true
        let library = try device.makeLibrary(source: shaderSource, options: compileOptions)
        guard let lensMapFunction = library.makeFunction(name: "traceLensMap") else {
            throw RendererError.missingFunction("traceLensMap")
        }
        guard let shadeFunction = library.makeFunction(name: "shadeLensMap") else {
            throw RendererError.missingFunction("shadeLensMap")
        }
        guard let presentFunction = library.makeFunction(name: "presentHDR") else {
            throw RendererError.missingFunction("presentHDR")
        }

        self.view = view
        self.device = device
        self.commandQueue = commandQueue
        self.lensMapPipeline = try device.makeComputePipelineState(function: lensMapFunction)
        self.shadePipeline = try device.makeComputePipelineState(function: shadeFunction)
        self.presentPipeline = try device.makeComputePipelineState(function: presentFunction)
        self.configuration = configuration
        self.exposure = Float(configuration.exposure)
        self.overlayVisible = overlayVisible
        super.init()

        view.device = device
        view.delegate = self
        view.interactionDelegate = self
        view.colorPixelFormat = .rgba16Float
        view.depthStencilPixelFormat = .invalid
        view.framebufferOnly = false
        view.autoResizeDrawable = false
        view.drawableSize = CGSize(width: configuration.width, height: configuration.height)
        view.preferredFramesPerSecond = configuration.targetFPS
        view.enableSetNeedsDisplay = false
        view.isPaused = false
        view.clearColor = MTLClearColorMake(0, 0, 0, 1)

        configureEDR()
        refreshEDRHeadroom()
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(refreshEDRHeadroom),
            name: NSApplication.didChangeScreenParametersNotification,
            object: nil
        )
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(refreshEDRHeadroom),
            name: NSWindow.didChangeScreenNotification,
            object: nil
        )
        try prepareRenderTargets()
        precomputeLensMap()
    }

    var performanceSnapshot: PerformanceSnapshot { monitor.snapshot() }
    var renderScale: Double { 1 }
    var traceResolution: CGSize { CGSize(width: configuration.width, height: configuration.height) }
    var edrHeadroom: Double { cachedEDRHeadroom }

    @objc func refreshEDRHeadroom() {
        let screen = view.window?.screen ?? NSScreen.main
        let potential = screen?.maximumPotentialExtendedDynamicRangeColorComponentValue ?? 1
        let current = screen?.maximumExtendedDynamicRangeColorComponentValue ?? 1
        cachedEDRHeadroom = min(16, max(1, max(potential, current)))
    }

    private func configureEDR() {
        guard let metalLayer = view.layer as? CAMetalLayer else { return }
        metalLayer.pixelFormat = .rgba16Float
        metalLayer.framebufferOnly = false
        metalLayer.colorspace = CGColorSpace(name: CGColorSpace.extendedLinearDisplayP3)
        metalLayer.wantsExtendedDynamicRangeContent = true
        metalLayer.edrMetadata = CAEDRMetadata.hdr10(
            minLuminance: 0.0001,
            maxLuminance: 1_600,
            opticalOutputScale: 100
        )
        metalLayer.maximumDrawableCount = 3
        metalLayer.displaySyncEnabled = true
        metalLayer.allowsNextDrawableTimeout = true
    }

    private func prepareRenderTargets() throws {
        let mapDescriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba32Float,
            width: configuration.width,
            height: configuration.height,
            mipmapped: false
        )
        mapDescriptor.storageMode = .private
        mapDescriptor.usage = [.shaderRead, .shaderWrite]

        let radianceDescriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba16Float,
            width: configuration.width,
            height: configuration.height,
            mipmapped: false
        )
        radianceDescriptor.storageMode = .private
        radianceDescriptor.usage = [.shaderRead, .shaderWrite]

        guard let lensMap = device.makeTexture(descriptor: mapDescriptor),
              let radianceTexture = device.makeTexture(descriptor: radianceDescriptor) else {
            throw RendererError.textureAllocation
        }
        lensMap.label = "2560x1440 Kerr geodesic lens map"
        radianceTexture.label = "2560x1440 scene-linear HDR radiance"
        self.lensMap = lensMap
        self.radianceTexture = radianceTexture
    }

    private func precomputeLensMap() {
        guard let commandBuffer = commandQueue.makeCommandBuffer() else { return }
        var uniforms = makeUniforms()
        for phase in 0..<mapPhaseCount {
            encodeLensMapPhase(phase, uniforms: &uniforms, commandBuffer: commandBuffer)
        }
        commandBuffer.commit()
        commandBuffer.waitUntilCompleted()
        lensMapInitialized = commandBuffer.status == .completed
    }

    func draw(in view: MTKView) {
        guard inFlightSemaphore.wait(timeout: .now()) == .success else { return }
        guard let drawable = view.currentDrawable,
              let commandBuffer = commandQueue.makeCommandBuffer() else {
            inFlightSemaphore.signal()
            return
        }

        monitor.frameSubmitted()
        commandBuffer.label = "Full-resolution Kerr frame \(frameIndex)"
        var uniforms = makeUniforms()

        if !lensMapInitialized {
            for phase in 0..<mapPhaseCount {
                encodeLensMapPhase(phase, uniforms: &uniforms, commandBuffer: commandBuffer)
            }
            lensMapInitialized = true
        } else if pendingMapPhases > 0 {
            encodeLensMapPhase(nextMapPhase, uniforms: &uniforms, commandBuffer: commandBuffer)
            nextMapPhase = (nextMapPhase + 1) % mapPhaseCount
            pendingMapPhases -= 1
        }

        guard let shadeEncoder = commandBuffer.makeComputeCommandEncoder() else {
            inFlightSemaphore.signal()
            return
        }
        shadeEncoder.label = "Shade full-resolution lens map"
        shadeEncoder.setComputePipelineState(shadePipeline)
        shadeEncoder.setTexture(lensMap, index: 0)
        shadeEncoder.setTexture(radianceTexture, index: 1)
        shadeEncoder.setBytes(&uniforms, length: MemoryLayout<ShaderUniforms>.stride, index: 0)
        dispatch(
            encoder: shadeEncoder,
            pipeline: shadePipeline,
            width: configuration.width,
            height: configuration.height
        )
        shadeEncoder.endEncoding()

        guard let presentEncoder = commandBuffer.makeComputeCommandEncoder() else {
            inFlightSemaphore.signal()
            return
        }
        presentEncoder.label = "Map scene-linear HDR to XDR"
        presentEncoder.setComputePipelineState(presentPipeline)
        presentEncoder.setTexture(radianceTexture, index: 0)
        presentEncoder.setTexture(drawable.texture, index: 1)
        presentEncoder.setBytes(&uniforms, length: MemoryLayout<ShaderUniforms>.stride, index: 0)
        dispatch(
            encoder: presentEncoder,
            pipeline: presentPipeline,
            width: configuration.width,
            height: configuration.height
        )
        presentEncoder.endEncoding()

        commandBuffer.present(drawable)
        commandBuffer.addCompletedHandler { [weak self] completed in
            guard let self else { return }
            self.inFlightSemaphore.signal()
            let gpuSeconds = max(0, completed.gpuEndTime - completed.gpuStartTime)
            self.monitor.frameCompleted(gpuSeconds: gpuSeconds)
        }
        commandBuffer.commit()
        frameIndex &+= 1
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        // The output remains at the requested pixel resolution and is letterboxed by the container.
    }

    private func encodeLensMapPhase(
        _ phase: UInt32,
        uniforms: inout ShaderUniforms,
        commandBuffer: MTLCommandBuffer
    ) {
        guard let encoder = commandBuffer.makeComputeCommandEncoder() else { return }
        uniforms.sampling.z = phase
        encoder.label = "Trace full-resolution lens map phase \(phase + 1) of \(mapPhaseCount)"
        encoder.setComputePipelineState(lensMapPipeline)
        encoder.setTexture(lensMap, index: 0)
        encoder.setBytes(&uniforms, length: MemoryLayout<ShaderUniforms>.stride, index: 0)
        dispatch(
            encoder: encoder,
            pipeline: lensMapPipeline,
            width: (configuration.width + 3) / 4,
            height: (configuration.height + 1) / 2
        )
        encoder.endEncoding()
    }

    private func makeUniforms() -> ShaderUniforms {
        let now = CACurrentMediaTime()
        let animationTime = isAnimating ? Float(now - animationStarted) : frozenAnimationTime
        return ShaderUniforms(
            traceSize: SIMD2(UInt32(configuration.width), UInt32(configuration.height)),
            outputSize: SIMD2(UInt32(configuration.width), UInt32(configuration.height)),
            camera: SIMD4(animationTime, Float(configuration.spin), cameraInclination, cameraRadius),
            image: SIMD4(cameraFOV, exposure, Float(edrHeadroom), cameraAzimuth),
            sampling: SIMD4(frameIndex, UInt32(configuration.maxSteps), 0, 0),
            disk: SIMD4(
                Float(configuration.horizonRadius),
                Float(configuration.iscoRadius),
                11.5,
                20_000
            )
        )
    }

    private func dispatch(
        encoder: MTLComputeCommandEncoder,
        pipeline: MTLComputePipelineState,
        width: Int,
        height: Int
    ) {
        let threadWidth = pipeline.threadExecutionWidth
        let threadHeight = max(1, pipeline.maxTotalThreadsPerThreadgroup / threadWidth)
        encoder.dispatchThreads(
            MTLSize(width: width, height: height, depth: 1),
            threadsPerThreadgroup: MTLSize(width: threadWidth, height: threadHeight, depth: 1)
        )
    }

    private func invalidateLensMap() {
        if pendingMapPhases == 0 {
            nextMapPhase = 0
        }
        pendingMapPhases = mapPhaseCount
    }

    func orbit(deltaX: Float, deltaY: Float) {
        cameraAzimuth += deltaX * 0.006
        let previousInclination = cameraInclination
        cameraInclination = min(max(cameraInclination + deltaY * 0.003, 0.12), Float.pi - 0.12)
        if cameraInclination != previousInclination { invalidateLensMap() }
    }

    func zoom(delta: Float) {
        let zoomImpulse = min(max(delta * 0.008, -0.08), 0.08)
        let minimumFOV = Float(18 * Double.pi / 180)
        let maximumFOV = Float(100 * Double.pi / 180)
        let previousFOV = cameraFOV
        cameraFOV = min(max(cameraFOV * exp(zoomImpulse), minimumFOV), maximumFOV)
        if cameraFOV != previousFOV { invalidateLensMap() }
    }

    func toggleOverlay() {
        overlayVisible.toggle()
        overlayVisibilityChanged?(overlayVisible)
    }

    func resetCamera() {
        cameraInclination = 1.558
        cameraFOV = 46 * .pi / 180
        cameraAzimuth = 0
        exposure = Float(configuration.exposure)
        invalidateLensMap()
    }

    func adjustExposure(by delta: Float) {
        exposure = min(max(exposure + delta, 0.1), 4)
    }

    func toggleAnimation() {
        if isAnimating {
            frozenAnimationTime = Float(CACurrentMediaTime() - animationStarted)
        } else {
            animationStarted = CACurrentMediaTime() - Double(frozenAnimationTime)
        }
        isAnimating.toggle()
    }
}
