2026-05-25

Added permanent status bar at the bottom of the screen with a scrolling
marquee and version label.  The marquee is context-sensitive: it shows
window manager help when WM mode is active, selection mode help during
copy/paste, and a menu hint otherwise.  Version string moved from the
wallpaper to the status bar.

Replaced the monolithic dropdown menu with a menubar widget (VWM | Apps)
in the top panel.  The hamburger icon remains clickable.

Added GPM fake cursor overlay for Linux console (yellow block).

Added mouse-driven menubar interaction with click-to-open dropdowns.

2018-03-31

Built in terminals are no longer added to the mainmenu by default.
You will need to add them to your configuration manually.  See
the sample configuration for how to do that.
