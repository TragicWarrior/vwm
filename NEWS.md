2026-05-28

VWM now refuses to start if the terminal is smaller than 80x25.  A
message is printed with the detected size; --ignore-tty-size bypasses
the check.

Manage Apps Menu and Manage Hotkeys dialogs now recenter on terminal
resize.  A warning popup informs the user that shrinking the terminal
further will close the dialog and discard unsaved changes.  If the
terminal does become too small, the dialog closes automatically.
Widget canvases are recreated after resize to prevent drawing
corruption and stale mouse hit zones.

Manage Apps Menu was shrunk by 2 rows to fit an 80x25 terminal.
Mouse hit zones for dropdowns and buttons are now computed
dynamically from the layout rather than hardcoded, preventing
misalignment when dialog dimensions change.

Manage Apps Menu Remove button now removes the selected entry from
the listbox immediately.  The Add button opens the Edit popup with
defaults and only inserts a new entry on Apply.  Save shows a
confirmation popup ("Settings saved.") and keeps the dialog open.
Close (renamed from Cancel) prompts to discard unsaved changes when
modifications have been made.

Dropdown menus (VWM / Apps) now account for the scrollbar column
in width calculations, preventing clipped text.  Trailing separator
after the last category in the Apps menu is removed so all items
are reachable when scrolling.

Added STYLE_GUIDE.md documenting the three UI color themes (cyan
main dialogs, red warning/confirm popups, blue input/info popups)
and VDK patterns for client area fill, resize workarounds, and
widget recreation.

2026-05-27

Added Manage Hotkeys dialog accessible from VWM menu.  All 13 hotkeys
are displayed in a scrollable list grouped by category (Menu
Accelerators, Window Management, Navigation).  Hotkeys can be
reassigned by selecting one and pressing Test to capture a new key,
or Reset to restore the factory default.  Duplicate key detection
prevents saving conflicting bindings.  A Load button opens a file
chooser for importing hotkeys from an alternate config file.  Cancel
prompts to confirm if there are unsaved changes.

Hotkeys are now stored in the vwmrc config and persist across sessions.
The hardcoded key bindings in the window manager have been replaced
with configurable values loaded from the config file at startup.
Ctrl+arrow keys remain as hardcoded secondary bindings for resize
actions.

Ported the Edit App and Load Config popups in Manage Apps to use the
new vk_popup_t composite widget from VDK.  The popup handles window
creation, button bar layout, and interior structure internally.  Edit
popup buttons (Apply/Cancel) are now individually tab-stoppable.
Mouse clicks on popup buttons use dynamic hit testing based on button
count instead of hardcoded pixel positions.

2026-05-26

Fixed mouse clicks not reaching terminal applications (e.g. Midnight
Commander) after moving a window.  Coordinates are now adjusted for
the window position before forwarding to the PTY.  Also fixed simple
clicks not being forwarded at all due to mouseinterval(0) preventing
ncurses BUTTON1_CLICKED synthesis.

Added multiple desktops (3 surfaces) with per-surface window decks.
Alt+d opens a prompt to switch desktops by number (works on both
xterm and Linux console).  "Switch desktop (Alt d)" is also available
in the VWM dropdown menu.  Panel and status bar follow the active
surface.  Each desktop has a distinct checkerboard wallpaper (blue,
red, cyan).

Added "Reload Apps Menu" to the VWM dropdown menu.  This re-reads
~/.vwm/vwmrc and repopulates the Apps menu without restarting.

Replaced vwmterm help text in the window title bar with the module
name (e.g. "VTerm (xterm)").  Help is now shown in the status bar
marquee instead.

Added calendar popup triggered by clicking the clock in the panel.
The calendar shows an XFCE4-style month view with navigation via
arrow keys, mouse wheel, or clicking the < > arrows.  Escape or
clicking outside dismisses it.

Dropdown menu highlight now uses bright white on red for better
contrast.

Panel clock and task counter now populate immediately on startup
instead of flashing black for one second.

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
