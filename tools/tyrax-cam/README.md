# TyraX Cam (iOS companion app)

The phone half of the editor's **phone camera link**: your iPhone becomes a
viewfinder onto the TyraX viewport, and moving the phone moves that camera.
Press **Record** and the move is written into a Cutscene Director camera track.

The editor side and the protocol are documented in
[docs/phone-camera.md](../../docs/phone-camera.md).

> **Not verified on a device yet.** The editor side, the wire protocol and the
> stream have been tested end to end against the built-in browser test client
> (see below); this app itself has never been built on a Mac. Expect to fix
> small things on the first build — and please update this note when you do.

## What it does

- Connects to the editor over your LAN (`ws://<editor-ip>:7798`) with a 6-digit
  pairing code.
- Streams **ARKit world-tracking pose** (position in metres + rotation) at 30 Hz.
  6DoF: walking moves the camera, not just turning it.
- Shows the editor's live viewport image as a JPEG stream (quality/frame rate
  selectable — Low / Medium / High).
- **Record / Stop / Recentre** buttons, so you never have to reach the keyboard
  while holding the camera.
- Keeps the screen awake while connected, and warns when ARKit tracking degrades
  ("moving too fast", "not enough detail to track").

No camera image is ever captured, shown or transmitted. ARKit needs the camera
only to solve the device's motion, which is what the `NSCameraUsageDescription`
in `app.json` says.

## Layout

```
App.js                     both screens (connect, viewfinder)
src/protocol.js            the wire format: frame codec + base64
src/link.js                the WebSocket connection and the handshake
modules/tyrax-arkit/       local Expo module: ARKit -> JS pose events
  ios/TyraxArkitModule.swift
```

`ios/` and `android/` are **not** checked in — `expo prebuild` generates them.

## Building it (sideload, no App Store)

This is a private tool, so all three routes below are unsigned-store-free. You
need **a Mac with Xcode** for the first two; the app cannot be built on Windows.

### 1. Xcode with a free Apple ID (simplest, 7-day expiry)

```bash
cd tools/tyrax-cam
npm install
npx expo prebuild -p ios
open ios/TyraXCam.xcworkspace
```

In Xcode: select the `TyraXCam` target → *Signing & Capabilities* → tick
*Automatically manage signing* → **Team**: your personal Apple ID (add it under
*Xcode > Settings > Accounts* if it is not listed) → change **Bundle Identifier**
to something unique to you (e.g. `com.<yourname>.tyraxcam`; the placeholder in
`app.json` will collide). Plug the phone in, pick it as the run destination, hit
**Run**.

The first launch needs *Settings > General > VPN & Device Management > Developer
App > Trust*. A free account's provisioning profile expires after **7 days** —
re-run from Xcode to renew. A paid developer account ($99/yr) makes it a year.

Or skip Xcode's UI once signing is configured:

```bash
npx expo run:ios --device --configuration Release
```

### 2. An ad-hoc `.ipa` (install without Xcode afterwards)

With a paid Apple Developer account, `eas.json` here already has an `internal`
distribution profile:

```bash
npx eas-cli build -p ios --profile device
```

Register the phone's UDID with the account when prompted. The resulting `.ipa`
installs from the EAS link, or via Apple Configurator / `ideviceinstaller`.

### 3. Sideloadly / AltStore (free account, no Mac needed to *install*)

Someone still has to produce the `.ipa` on a Mac (route 1's *Product > Archive*,
or route 2). Given an `.ipa`, [Sideloadly](https://sideloadly.io) or
[AltStore](https://altstore.io) can sign and install it on Windows with a free
Apple ID — same 7-day expiry, refreshed by the tool.

### Expo Go does not work

The ARKit module is native code, so Expo Go cannot load it. You need a native
build (any route above). The app will *run* under Expo Go and show the stream,
but the camera will not move and it says so on screen.

## Using it

1. In the editor: **Tools > Phone Camera** → *Start link*. It shows the address
   and the pairing code.
2. In the app: type that address and code → *Connect*.
3. Point the phone. The editor viewport (and the phone screen) follow it.
   *Recentre* puts the camera back at the editor's own viewpoint and aims it
   where the viewport was looking — everything the phone does is relative to
   that point.
4. To record: **Tools > Cutscene Director**, select a cutscene, open the
   *Phone camera* section, choose the target camera and the keyframe density,
   then press *Record* there or on the phone.

## Troubleshooting

| Symptom | Cause |
|---|---|
| "cannot reach the editor" | Different Wi-Fi networks, or Windows Firewall is blocking the editor. Allow `tyrax-editor.exe` on private networks. |
| "wrong pairing code" | The editor regenerated it (*New code* restarts pairing), or the link was restarted. |
| "another device is already connected" | One device at a time. Use *Disconnect* in the editor's Phone Camera window. |
| Picture updates but the camera never moves | No ARKit — see the on-screen warning; you are probably in Expo Go. |
| "limited: moving too fast" | ARKit lost the solve. Slow down, and film somewhere with visible detail (a blank white wall gives it nothing to track). |
| Stream stutters | Drop to the *Low* preset, or lower the fps in the editor's Phone Camera window. |
