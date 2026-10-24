# dmgbuild settings for the TyraX disk image (installer/build-macos.sh):
#
#   dmgbuild -s installer/macos/dmg-settings.py -D app=<TyraX.app> \
#            -D background=<tiff> -D volicon=<icns> -D layout=<dmg_layout.py> \
#            "TyraX" <out.dmg>
#
# dmgbuild writes the window's .DS_Store itself instead of scripting Finder,
# which is what makes the layout reliable on a headless CI runner. The window
# is the app on the left, an Applications link on the right and the art from
# make-dmg-background.py behind them (geometry: dmg_layout.py).
import os.path
import runpy

layout = runpy.run_path(defines["layout"])  # noqa: F821 - dmgbuild's global

app = defines["app"]  # noqa: F821
app_name = os.path.basename(app)

format = "ULFO"  # lzfse: smallest, opens on every macOS the app supports
filesystem = "HFS+"
files = [app]
symlinks = {"Applications": "/Applications"}
icon = defines["volicon"]  # noqa: F821
background = defines["background"]  # noqa: F821

show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
sidebar_width = 0

window_rect = ((200, 140), layout["WINDOW"])
default_view = "icon-view"
show_icon_preview = False
include_icon_view_settings = True
include_list_view_settings = False

arrange_by = None
grid_offset = (0, 0)
grid_spacing = 100
scroll_position = (0, 0)
label_pos = "bottom"
text_size = 13
icon_size = layout["ICON_SIZE"]
icon_locations = {
    app_name: layout["APP_POS"],
    "Applications": layout["APPS_POS"],
}
