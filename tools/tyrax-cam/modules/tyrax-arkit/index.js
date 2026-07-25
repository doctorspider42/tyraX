// JS face of the ARKit pose module. Optional on purpose: the app must still run
// (connect, show the stream) on a device or simulator without world tracking,
// telling the user what it cannot do rather than crashing on import.
import { requireOptionalNativeModule } from 'expo-modules-core';

const native = requireOptionalNativeModule('TyraxArkit');

export const available = !!native;

export function isSupported() {
  return !!native && native.isSupported();
}

// hz caps the pose rate handed to JS (ARKit itself runs at 60). The editor
// resamples to the keyframe density, so 30 is plenty.
export function start(hz = 30) {
  if (native) native.start(hz);
}

export function stop() {
  if (native) native.stop();
}

// Re-origins ARKit's world on the current pose (recovery from a bad solve).
// Not the same thing as the editor's "recentre", which only re-anchors the
// mapping and leaves tracking alone.
export function reset() {
  if (native) native.reset();
}

// listener({ ts, px, py, pz, qx, qy, qz, qw, fov, tracking }) -> subscription
export function addPoseListener(listener) {
  return native ? native.addListener('onPose', listener) : { remove() {} };
}

// listener({ state }) - "normal", "initialising", "limited: ...", "failed: ..."
export function addTrackingListener(listener) {
  return native ? native.addListener('onTracking', listener) : { remove() {} };
}
