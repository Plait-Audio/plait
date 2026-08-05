# dmgbuild settings for the branded ISO Drums installer DMG.
# Invoked by scripts/make_dmg.sh via:
#   dmgbuild -s scripts/dmg_settings.py -D srcdir=<dir> -D bg=<tiff> "<vol>" out.dmg
import os

srcdir = defines["srcdir"]          # folder with the three signed bundles
bg     = defines["bg"]              # HiDPI background.tiff

files = [
    os.path.join(srcdir, "ISO Drums.app"),
    os.path.join(srcdir, "ISO Drums.component"),
    os.path.join(srcdir, "ISO Drums.vst3"),
]
symlinks = {"Applications": "/Applications"}

icon_locations = {
    "ISO Drums.app":       (175, 195),
    "Applications":        (505, 195),
    "ISO Drums.component": (255, 400),
    "ISO Drums.vst3":      (425, 400),
}

background   = bg
window_rect  = ((200, 110), (680, 600))   # tall on purpose: content area stays well
                                           # below the fold so nothing ever scrolls
default_view = "icon-view"
icon_size    = 90
text_size    = 13

show_status_bar = False
show_pathbar    = False
show_sidebar    = False
show_toolbar    = False
show_icon_preview = False

format = "UDZO"
compression_level = 9
