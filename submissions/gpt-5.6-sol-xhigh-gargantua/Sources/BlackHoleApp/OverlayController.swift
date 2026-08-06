import AppKit
import BlackHoleCore

final class OverlayController {
    let view: NSView
    private let titleLabel = NSTextField(labelWithString: "GARGANTUA")
    private let detailLabel = NSTextField(labelWithString: "")
    private let statsLabel = NSTextField(labelWithString: "INITIALIZING METAL")
    private let hdrLabel = NSTextField(labelWithString: "HDR")
    private var timer: Timer?
    private let snapshot: () -> PerformanceSnapshot
    private let renderScale: () -> Double
    private let headroom: () -> Double

    init(
        view: NSView,
        configuration: RenderConfiguration,
        snapshot: @escaping () -> PerformanceSnapshot,
        renderScale: @escaping () -> Double,
        headroom: @escaping () -> Double
    ) {
        self.view = view
        self.snapshot = snapshot
        self.renderScale = renderScale
        self.headroom = headroom

        let horizon = configuration.horizonRadius
        let isco = configuration.iscoRadius
        detailLabel.stringValue = String(
            format: "KERR  a/M %.3f    r+ %.3fM    ISCO %.3fM",
            configuration.spin,
            horizon,
            isco
        )

        [titleLabel, detailLabel, statsLabel, hdrLabel].forEach {
            $0.translatesAutoresizingMaskIntoConstraints = false
            $0.textColor = .white
            $0.drawsBackground = false
            $0.isBezeled = false
            $0.isSelectable = false
            $0.shadow = {
                let shadow = NSShadow()
                shadow.shadowColor = NSColor.black.withAlphaComponent(0.9)
                shadow.shadowBlurRadius = 5
                shadow.shadowOffset = .zero
                return shadow
            }()
            view.addSubview($0)
        }

        titleLabel.font = .systemFont(ofSize: 18, weight: .semibold)
        detailLabel.font = .monospacedSystemFont(ofSize: 10, weight: .medium)
        detailLabel.textColor = NSColor.white.withAlphaComponent(0.72)
        statsLabel.font = .monospacedDigitSystemFont(ofSize: 11, weight: .medium)
        statsLabel.textColor = NSColor.white.withAlphaComponent(0.8)
        hdrLabel.font = .monospacedSystemFont(ofSize: 10, weight: .semibold)
        hdrLabel.alignment = .right

        NSLayoutConstraint.activate([
            titleLabel.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 26),
            titleLabel.topAnchor.constraint(equalTo: view.topAnchor, constant: 22),
            detailLabel.leadingAnchor.constraint(equalTo: titleLabel.leadingAnchor),
            detailLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 3),
            statsLabel.leadingAnchor.constraint(equalTo: titleLabel.leadingAnchor),
            statsLabel.bottomAnchor.constraint(equalTo: view.bottomAnchor, constant: -22),
            hdrLabel.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -26),
            hdrLabel.topAnchor.constraint(equalTo: titleLabel.topAnchor, constant: 3),
        ])

        timer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            self?.update()
        }
        update()
    }

    deinit { timer?.invalidate() }

    func setVisible(_ visible: Bool) {
        view.isHidden = !visible
    }

    private func update() {
        let current = snapshot()
        statsLabel.stringValue = String(
            format: "%3.0f FPS    %4.1f ms GPU    %2.0f%% TRACE",
            current.fps,
            current.averageGPUms,
            renderScale() * 100
        )
        let nits = max(100, min(1_600, headroom() * 100))
        hdrLabel.stringValue = String(format: "XDR  %.0f NITS", nits)
    }
}
