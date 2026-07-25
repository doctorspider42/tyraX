import ARKit
import ExpoModulesCore

// ARKit world tracking -> the pose stream the TyraX editor consumes.
//
// The editor's canonical take space (see src/camtake.hpp) is exactly ARKit's
// world convention - right-handed, Y up (gravity aligned), metres, camera
// looking down its own -Z - so nothing is converted here. The one thing worth
// knowing: ARKit defines the camera's local axes for a device held in
// LANDSCAPE RIGHT, so in portrait the reported rotation carries a 90-degree
// roll. That is harmless, because a camera keyframe stores eye + look-at only
// and rolling about the view axis does not move -Z. Roll is dropped downstream,
// deliberately.
//
// No camera image is ever read, shown or transmitted; ARKit needs the camera
// only to solve the motion.
public class TyraxArkitModule: Module {
  private var session: ARSession?
  private var delegate: PoseDelegate?

  public func definition() -> ModuleDefinition {
    Name("TyraxArkit")

    Events("onPose", "onTracking")

    Function("isSupported") { () -> Bool in
      ARWorldTrackingConfiguration.isSupported
    }

    // hz caps how often a pose is forwarded to JS. ARKit runs at 60; the editor
    // resamples to the keyframe density anyway, so 30 is plenty and halves the
    // bridge traffic.
    Function("start") { (hz: Double) in
      self.startSession(hz: hz)
    }

    Function("stop") {
      self.stopSession()
    }

    // Re-origins world tracking on the device's current pose. The editor has its
    // own "recentre" (it re-anchors the mapping), so this is only for recovering
    // from a bad tracking solve - after a reset the stream starts from zero.
    Function("reset") {
      guard let session = self.session else { return }
      session.run(Self.configuration(),
                  options: [.resetTracking, .removeExistingAnchors])
    }

    OnDestroy {
      self.stopSession()
    }
  }

  private static func configuration() -> ARWorldTrackingConfiguration {
    let cfg = ARWorldTrackingConfiguration()
    cfg.worldAlignment = .gravity  // +Y is up; +Z is wherever the phone started
    cfg.planeDetection = []        // nothing but the pose is wanted
    cfg.isLightEstimationEnabled = false
    if #available(iOS 13.0, *) {
      cfg.frameSemantics = []
    }
    return cfg
  }

  private func startSession(hz: Double) {
    stopSession()
    guard ARWorldTrackingConfiguration.isSupported else {
      sendEvent("onTracking", ["state": "unsupported"])
      return
    }
    let session = ARSession()
    let delegate = PoseDelegate(minInterval: hz > 0 ? 1.0 / hz : 0) { [weak self] payload in
      self?.sendEvent("onPose", payload)
    } onState: { [weak self] state in
      self?.sendEvent("onTracking", ["state": state])
    }
    session.delegate = delegate
    self.session = session
    self.delegate = delegate
    session.run(Self.configuration(), options: [.resetTracking, .removeExistingAnchors])
  }

  private func stopSession() {
    session?.pause()
    session?.delegate = nil
    session = nil
    delegate = nil
  }
}

private class PoseDelegate: NSObject, ARSessionDelegate {
  private let minInterval: Double
  private let onPose: ([String: Any]) -> Void
  private let onState: (String) -> Void
  private var lastSent: TimeInterval = 0
  private var startedAt: TimeInterval = -1
  private var lastState = ""

  init(minInterval: Double,
       onPose: @escaping ([String: Any]) -> Void,
       onState: @escaping (String) -> Void) {
    self.minInterval = minInterval
    self.onPose = onPose
    self.onState = onState
  }

  func session(_ session: ARSession, didUpdate frame: ARFrame) {
    // Tracking state changes are worth surfacing: "limited (excessive motion)"
    // is exactly what a hand-held camera move provokes, and the operator can
    // only slow down if the app tells them.
    let state = Self.describe(frame.camera.trackingState)
    if state != lastState {
      lastState = state
      onState(state)
    }

    if minInterval > 0 && frame.timestamp - lastSent < minInterval { return }
    lastSent = frame.timestamp
    if startedAt < 0 { startedAt = frame.timestamp }

    let m = frame.camera.transform
    let q = simd_quatf(m)
    // Vertical field of view of the captured image, from the intrinsics. ARKit
    // captures landscape, so this is the SHORT edge's angle - roughly 39 deg on
    // a wide lens. Sent for information; the app only forwards it when the user
    // asks to match the phone's lens.
    let fy = frame.camera.intrinsics[1][1]
    let imageH = Float(frame.camera.imageResolution.height)
    let fovDeg = fy > 0 ? 2 * atan(imageH / (2 * fy)) * 180 / Float.pi : 0

    onPose([
      "ts": frame.timestamp - startedAt,
      "px": m.columns.3.x, "py": m.columns.3.y, "pz": m.columns.3.z,
      "qx": q.imag.x, "qy": q.imag.y, "qz": q.imag.z, "qw": q.real,
      "fov": fovDeg,
      "tracking": state,
    ])
  }

  func session(_ session: ARSession, didFailWithError error: Error) {
    onState("failed: \(error.localizedDescription)")
  }

  func sessionWasInterrupted(_ session: ARSession) { onState("interrupted") }
  func sessionInterruptionEnded(_ session: ARSession) { lastState = "" }

  private static func describe(_ s: ARCamera.TrackingState) -> String {
    switch s {
    case .normal: return "normal"
    case .notAvailable: return "initialising"
    case .limited(let reason):
      switch reason {
      case .initializing: return "initialising"
      case .excessiveMotion: return "limited: moving too fast"
      case .insufficientFeatures: return "limited: not enough detail to track"
      case .relocalizing: return "relocalising"
      @unknown default: return "limited"
      }
    }
  }
}
