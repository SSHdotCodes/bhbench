import AppKit
import MetalKit

final class BlackHoleView: MTKView {
    weak var interactionDelegate: BlackHoleInteractionDelegate?

    override var acceptsFirstResponder: Bool { true }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
    }

    override func mouseDragged(with event: NSEvent) {
        guard NSEvent.pressedMouseButtons & 1 == 1 else { return }
        interactionDelegate?.orbit(deltaX: Float(event.deltaX), deltaY: Float(event.deltaY))
    }

    override func scrollWheel(with event: NSEvent) {
        interactionDelegate?.zoom(delta: Float(event.scrollingDeltaY))
    }

    override func keyDown(with event: NSEvent) {
        guard let character = event.charactersIgnoringModifiers?.lowercased() else { return }
        switch character {
        case "f": window?.toggleFullScreen(nil)
        case "h": interactionDelegate?.toggleOverlay()
        case "r": interactionDelegate?.resetCamera()
        case "[": interactionDelegate?.adjustExposure(by: -0.08)
        case "]": interactionDelegate?.adjustExposure(by: 0.08)
        case " ": interactionDelegate?.toggleAnimation()
        default: super.keyDown(with: event)
        }
    }
}

protocol BlackHoleInteractionDelegate: AnyObject {
    func orbit(deltaX: Float, deltaY: Float)
    func zoom(delta: Float)
    func toggleOverlay()
    func resetCamera()
    func adjustExposure(by delta: Float)
    func toggleAnimation()
}

final class RenderContainerView: NSView {
    let metalView: BlackHoleView
    let overlayView = PassthroughView()

    init(metalView: BlackHoleView) {
        self.metalView = metalView
        super.init(frame: .zero)
        wantsLayer = true
        layer?.backgroundColor = NSColor.black.cgColor
        addSubview(metalView)
        addSubview(overlayView)
    }

    required init?(coder: NSCoder) { nil }

    override func layout() {
        super.layout()
        let targetAspect = metalView.drawableSize.width / max(metalView.drawableSize.height, 1)
        var fitted = bounds
        if bounds.width / max(bounds.height, 1) > targetAspect {
            fitted.size.width = bounds.height * targetAspect
            fitted.origin.x = (bounds.width - fitted.width) * 0.5
        } else {
            fitted.size.height = bounds.width / targetAspect
            fitted.origin.y = (bounds.height - fitted.height) * 0.5
        }
        metalView.frame = fitted.integral
        overlayView.frame = fitted.integral
    }
}

final class PassthroughView: NSView {
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}
