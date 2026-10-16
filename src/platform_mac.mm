// The macOS half of platform.hpp's merged title bar - the only Objective-C in
// the editor, because NSWindow has no C API. Everything else macOS-specific
// stays in platform.cpp's __APPLE__ branches; this file holds exactly the
// three title-bar functions platform.cpp leaves out on Apple.
//
// What it does to GLFW's NSWindow (docs/editor-theme.md, "Title bar"):
//   - titlebarAppearsTransparent + titleVisibility hidden + a full-size
//     content view: the content (our GL view) now covers the title bar row,
//     and AppKit draws only the traffic lights, over our top bar;
//   - the traffic lights are re-centred on our bar's height (their container
//     view made as tall as the bar), every frame it differs and on every
//     resize - AppKit puts them back at its own 28 pt row whenever it re-lays
//     out the title bar;
//   - a local NSEvent monitor turns a left mouse-down on a bare part of the
//     bar into the system window drag (performWindowDragWithEvent:) and a
//     double click into the action System Settings > Desktop & Dock names
//     (AppleActionOnDoubleClick), and swallows it so ImGui never sees it.
#include "platform.hpp"

#import <Cocoa/Cocoa.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <cmath>

namespace platform {

namespace {

struct MacTitleBar {
    NSWindow* window = nil;
    CGFloat height = 0.0;    // our bar, points (App, per frame)
    bool overEmpty = false;  // App: the pointer is over a bare part of the bar
    id mouseMonitor = nil;
    id resizeObserver = nil;
};
MacTitleBar g_bar;

bool inFullScreen(NSWindow* w) {
    return ([w styleMask] & NSWindowStyleMaskFullScreen) != 0;
}

// The three buttons' common superview (NSTitlebarView) and ITS superview
// (NSTitlebarContainerView, the full-width strip AppKit sizes to 28 pt).
// Private classes, reached only through the public standardWindowButton - the
// same route Tauri's traffic_light_position and Electron's
// trafficLightPosition take.
NSView* titlebarContainer(NSWindow* w) {
    NSButton* close = [w standardWindowButton:NSWindowCloseButton];
    return close ? close.superview.superview : nil;
}

// Centre the traffic lights on a bar `height` points tall. Idempotent and
// cheap when nothing changed (three frame reads), so it can run every frame.
void layoutTrafficLights() {
    NSWindow* w = g_bar.window;
    if (!w || g_bar.height <= 0.0 || inFullScreen(w)) return;
    NSButton* close = [w standardWindowButton:NSWindowCloseButton];
    NSView* container = titlebarContainer(w);
    if (!close || !container) return;
    const CGFloat buttonH = NSHeight(close.frame);
    const CGFloat h = std::max(std::round(g_bar.height), buttonH);
    // The container hangs from the window's top edge (non-flipped coordinates:
    // origin bottom-left), so a taller one grows downwards over our bar.
    NSRect cf = container.frame;
    const CGFloat top = NSHeight(w.frame);
    if (NSHeight(cf) != h || NSMaxY(cf) != top) {
        cf.size.height = h;
        cf.origin.y = top - h;
        container.frame = cf;
    }
    NSView* titlebar = close.superview;
    if (NSHeight(titlebar.frame) != h) {
        NSRect tf = titlebar.frame;
        tf.origin.y = 0.0;
        tf.size.height = h;
        titlebar.frame = tf;
    }
    const CGFloat y = std::round((h - buttonH) * 0.5);
    for (NSWindowButton kind :
         {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
        NSButton* b = [w standardWindowButton:kind];
        if (b && b.frame.origin.y != y) [b setFrameOrigin:NSMakePoint(b.frame.origin.x, y)];
    }
}

// System Settings > Desktop & Dock > "Double-click a window's title bar to":
// Zoom ("Maximize", also the default when unset), Minimize, Fill (macOS 15+,
// treated as Zoom - Fill has no public API) or Do Nothing ("None").
void titleBarDoubleClick(NSWindow* w) {
    NSString* action =
        [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleActionOnDoubleClick"];
    if ([action isEqualToString:@"None"]) return;
    if ([action isEqualToString:@"Minimize"]) {
        [w performMiniaturize:nil];
        return;
    }
    if (!action && [[NSUserDefaults standardUserDefaults] boolForKey:@"AppleMiniaturizeOnDoubleClick"]) {
        [w performMiniaturize:nil];  // the pre-10.11 spelling of the same switch
        return;
    }
    [w performZoom:nil];
}

}  // namespace

bool mergeTitleBar(GLFWwindow* window) {
    NSWindow* w = window ? glfwGetCocoaWindow(window) : nil;
    if (!w) return false;
    if (g_bar.window) return g_bar.window == w;  // one main window
    g_bar.window = w;

    w.titlebarAppearsTransparent = YES;
    w.titleVisibility = NSWindowTitleHidden;  // still the Window menu's / Mission Control's name
    w.styleMask = w.styleMask | NSWindowStyleMaskFullSizeContentView;

    // AppKit re-lays out the title bar on every resize, and a live resize runs
    // in a tracking loop that never returns to the editor's frame loop - so
    // the per-frame layout alone would leave the lights at their 28 pt row for
    // the whole drag.
    g_bar.resizeObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSWindowDidResizeNotification
                    object:w
                     queue:nil
                usingBlock:^(NSNotification*) {
                  layoutTrafficLights();
                }];

    g_bar.mouseMonitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown
                                     handler:^NSEvent*(NSEvent* e) {
                                       NSWindow* win = g_bar.window;
                                       if (e.window != win || !g_bar.overEmpty || inFullScreen(win))
                                           return e;
                                       // Content coordinates are bottom-up; the bar is at the top.
                                       const CGFloat fromTop =
                                           NSHeight(win.contentView.frame) - e.locationInWindow.y;
                                       if (fromTop < 0.0 || fromTop >= g_bar.height) return e;
                                       if (e.clickCount == 2) titleBarDoubleClick(win);
                                       else [win performWindowDragWithEvent:e];
                                       return nil;  // ImGui never sees the press
                                     }];
    return true;
}

void updateTitleBar(GLFWwindow* window, const TitleBarFrame& frame) {
    (void)window;
    if (!g_bar.window) return;
    g_bar.height = frame.height;
    g_bar.overEmpty = frame.overEmpty;
    layoutTrafficLights();
}

float titleBarInsetLeft(GLFWwindow* window) {
    (void)window;
    NSWindow* w = g_bar.window;
    if (!w || inFullScreen(w)) return 0.0f;
    NSButton* zoom = [w standardWindowButton:NSWindowZoomButton];
    if (!zoom || zoom.hidden) return 0.0f;
    // The zoom button's right edge in window coordinates, plus the same gap
    // AppKit leaves on the lights' left (the close button's x).
    const NSRect r = [zoom convertRect:zoom.bounds toView:nil];
    NSButton* close = [w standardWindowButton:NSWindowCloseButton];
    const CGFloat gap = close ? [close convertRect:close.bounds toView:nil].origin.x : 8.0;
    return (float)(NSMaxX(r) + gap);
}

}  // namespace platform
