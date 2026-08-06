import BlackHoleCore
import Foundation

struct AppOptions {
    var configuration = RenderConfiguration()
    var fullscreen = false
    var showOverlay = true

    init(arguments: [String] = Array(CommandLine.arguments.dropFirst())) {
        var index = 0
        while index < arguments.count {
            let argument = arguments[index]
            func value() -> String? {
                guard index + 1 < arguments.count else { return nil }
                index += 1
                return arguments[index]
            }

            switch argument {
            case "--width": configuration.width = Int(value() ?? "") ?? configuration.width
            case "--height": configuration.height = Int(value() ?? "") ?? configuration.height
            case "--target-fps": configuration.targetFPS = Int(value() ?? "") ?? configuration.targetFPS
            case "--render-scale": configuration.renderScale = Double(value() ?? "") ?? configuration.renderScale
            case "--steps": configuration.maxSteps = Int(value() ?? "") ?? configuration.maxSteps
            case "--spin": configuration.spin = Double(value() ?? "") ?? configuration.spin
            case "--exposure": configuration.exposure = Double(value() ?? "") ?? configuration.exposure
            case "--benchmark-seconds": configuration.benchmarkSeconds = Double(value() ?? "")
            case "--no-adaptive": configuration.adaptiveQuality = false
            case "--fullscreen": fullscreen = true
            case "--no-overlay": showOverlay = false
            default: break
            }
            index += 1
        }

        configuration.width = min(max(configuration.width, 640), 7680)
        configuration.height = min(max(configuration.height, 360), 4320)
        configuration.targetFPS = min(max(configuration.targetFPS, 30), 240)
        configuration.renderScale = min(max(configuration.renderScale, 0.25), 1)
        configuration.maxSteps = min(max(configuration.maxSteps, 24), 192)
        configuration.spin = min(max(configuration.spin, -0.999), 0.999)
        configuration.exposure = min(max(configuration.exposure, 0.1), 8)
    }
}
