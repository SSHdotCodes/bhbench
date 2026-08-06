import Foundation

public enum KerrPhysics {
    public static func eventHorizon(spin: Double) -> Double {
        let a = min(max(abs(spin), 0), 0.999999)
        return 1 + sqrt(1 - a * a)
    }

    public static func progradeISCO(spin: Double) -> Double {
        let a = min(max(spin, -0.999999), 0.999999)
        let z1 = 1 + cbrt(1 - a * a) * (cbrt(1 + a) + cbrt(1 - a))
        let z2 = sqrt(3 * a * a + z1 * z1)
        let direction = a >= 0 ? 1.0 : -1.0
        return 3 + z2 - direction * sqrt((3 - z1) * (3 + z1 + 2 * z2))
    }
}

public struct RenderConfiguration: Equatable {
    public var width: Int
    public var height: Int
    public var targetFPS: Int
    public var renderScale: Double
    public var maxSteps: Int
    public var spin: Double
    public var exposure: Double
    public var adaptiveQuality: Bool
    public var benchmarkSeconds: Double?

    public init(
        width: Int = 2560,
        height: Int = 1440,
        targetFPS: Int = 120,
        renderScale: Double = 1.0,
        maxSteps: Int = 192,
        spin: Double = 0.998,
        exposure: Double = 1.0,
        adaptiveQuality: Bool = true,
        benchmarkSeconds: Double? = nil
    ) {
        self.width = width
        self.height = height
        self.targetFPS = targetFPS
        self.renderScale = renderScale
        self.maxSteps = maxSteps
        self.spin = spin
        self.exposure = exposure
        self.adaptiveQuality = adaptiveQuality
        self.benchmarkSeconds = benchmarkSeconds
    }

    public var horizonRadius: Double { KerrPhysics.eventHorizon(spin: spin) }
    public var iscoRadius: Double { KerrPhysics.progradeISCO(spin: spin) }
}
