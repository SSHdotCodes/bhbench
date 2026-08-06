import XCTest
@testable import BlackHoleCore

final class PhysicsTests: XCTestCase {
    func testSchwarzschildLimits() {
        XCTAssertEqual(KerrPhysics.eventHorizon(spin: 0), 2, accuracy: 1e-12)
        XCTAssertEqual(KerrPhysics.progradeISCO(spin: 0), 6, accuracy: 1e-12)
    }

    func testInterstellarLikeSpinContractsOrbitAndHorizon() {
        let horizon = KerrPhysics.eventHorizon(spin: 0.998)
        let isco = KerrPhysics.progradeISCO(spin: 0.998)
        XCTAssertEqual(horizon, 1.0632139, accuracy: 1e-6)
        XCTAssertEqual(isco, 1.2369707, accuracy: 1e-6)
        XCTAssertGreaterThan(isco, horizon)
    }

    func testSpinIsClampedBelowExtremal() {
        XCTAssertTrue(KerrPhysics.eventHorizon(spin: 4).isFinite)
        XCTAssertTrue(KerrPhysics.progradeISCO(spin: 4).isFinite)
    }

    func testRenderDefaultsAre1440p120() {
        let configuration = RenderConfiguration()
        XCTAssertEqual(configuration.width, 2560)
        XCTAssertEqual(configuration.height, 1440)
        XCTAssertEqual(configuration.targetFPS, 120)
        XCTAssertEqual(configuration.renderScale, 1)
        XCTAssertEqual(configuration.maxSteps, 192)
        XCTAssertTrue(configuration.adaptiveQuality)
    }
}
