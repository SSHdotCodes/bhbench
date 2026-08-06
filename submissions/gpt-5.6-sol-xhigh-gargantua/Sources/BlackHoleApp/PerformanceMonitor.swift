import Foundation
import QuartzCore

struct PerformanceSnapshot {
    let fps: Double
    let averageGPUms: Double
    let p95GPUms: Double
    let frameCount: Int
}

final class PerformanceMonitor {
    private let lock = NSLock()
    private var gpuTimes: [Double] = []
    private var frameTimes: [Double] = []
    private var previousFrameTime: CFTimeInterval?
    private var completedFrames = 0

    func frameSubmitted(at time: CFTimeInterval = CACurrentMediaTime()) {
        lock.lock()
        if let previousFrameTime {
            frameTimes.append(time - previousFrameTime)
            if frameTimes.count > 720 { frameTimes.removeFirst(frameTimes.count - 720) }
        }
        previousFrameTime = time
        lock.unlock()
    }

    func frameCompleted(gpuSeconds: Double) {
        lock.lock()
        completedFrames += 1
        if gpuSeconds > 0 {
            gpuTimes.append(gpuSeconds * 1_000)
            if gpuTimes.count > 720 { gpuTimes.removeFirst(gpuTimes.count - 720) }
        }
        lock.unlock()
    }

    func snapshot(last sampleCount: Int = 360) -> PerformanceSnapshot {
        lock.lock()
        let gpu = Array(gpuTimes.suffix(sampleCount))
        let frames = Array(frameTimes.suffix(sampleCount))
        let count = completedFrames
        lock.unlock()

        let fps = frames.isEmpty ? 0 : 1 / (frames.reduce(0, +) / Double(frames.count))
        let average = gpu.isEmpty ? 0 : gpu.reduce(0, +) / Double(gpu.count)
        let sorted = gpu.sorted()
        let p95Index = sorted.isEmpty ? 0 : min(sorted.count - 1, Int(Double(sorted.count - 1) * 0.95))
        return PerformanceSnapshot(
            fps: fps,
            averageGPUms: average,
            p95GPUms: sorted.isEmpty ? 0 : sorted[p95Index],
            frameCount: count
        )
    }
}
