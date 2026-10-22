# Source-confirmed resume action

DebuggerWindow::actionRun has **no keyboard shortcut** in exact v2.9.93 DebuggerWindow.ui; there is no source-supported F5 Run binding to assume. Its paused label is Run and running label Pause. DebuggerWindow.cpp connects that action to onRunPause(), which invokes EmuThread::setVMPaused(!QtHost::IsVMPaused()). The toolbar and Debug menu expose the same action. Step Into is F11, Step Over F10 and Step Out Shift+F11; none resumes ordinary continuous execution. The Debug menu title has no explicit ampersand mnemonic, so Alt+D is not a source-guaranteed shortcut either.

A concrete keyboard route is the existing **TogglePause = Keyboard/Space** profile binding. Exact Pad.cpp installs that default; the historical owned profile also explicitly contains it. Hotkeys.cpp handles key release and toggles VMManager paused state when a valid VM exists and CanPause permits it. DisplaySurface::handleKeyInputEvent forwards key press/release through Host::RunOnCPUThread to InputManager::InvokeEvents, and its QWindow/container event handlers cover render-surface keyboard input. This is a render-window hotkey route, not a debugger-widget F5 action.

Root may retain or explicitly set this binding in its new owned profile before boot, pin profile bytes and verify achievements hardcore/pause cooldown is inactive. After -debugger stops on the actual ELF entry and the one enabled halt breakpoint is loaded, identify the exact owned render-surface window/container (not the debugger edit widget) using that launch PID, live window inventory/title/class/parent chain and _NET_WM_PID. Never pick the first matching PID window without identifying its render role. Source identifies debugger title PCSX2 Debugger; render identity still requires the owned live window inventory.

Root-only invocation template after ownership/target verification:

```
DISPLAY=OWNED_DISPLAY xdotool key --window OWNED_RENDER_SURFACE_ID --clearmodifiers space
```

Send one complete press/release pair only from confirmed paused initial-entry state. Since this is a toggle, blindly retrying can undo a successful resume or release the final halt. Require actual READY protocol and final paused status1/exact halt PC/accepted EE marker via the capture helper; the source keyboard route itself is not proof that X11 delivered the key. Initial and final PINE status can both be1 if the short probe runs between polls, so a missing observed running0 alone is not a rejection or success. If key delivery is unsuccessful, use the owned debugger Run action/toolbar from current observed UI, without historical coordinates, and preserve the failed attempt.

No keyboard, UI, window enumeration or device action was executed while preparing this report. No saved profile was changed. This is read-only source guidance for root's owned control.
