2026-05-29

Improved mouse responsiveness on the Linux console.  The scheduler now
dispatches the HIGH (input) queue every step instead of throttling it as
more vterms are opened, so input stays responsive regardless of how many
terminals are running (previously it noticeably degraded once a second
vterm was open).  Window drags also coalesce queued GPM move events down
to the latest cursor position -- one move plus one repaint per cycle
instead of replaying a backlog -- so a dragged window tracks the cursor
even under heavy terminal output.

Added a Print File tool (the vwmprint module) to the VWM menu, next to
"Capture screenshot".  It opens a system dialog to pick a .txt, .md, or
.pdf file, then choose an already-configured CUPS printer from a framed
list with Print/Cancel buttons, and prints it via libcups.  Like the
other system tools (Manage Apps/Hotkeys/Settings, the screensaver) it is
an unmanaged surface overlay rather than a deck-managed window: a new
vwm->tool_window hook lets a loadable module grab input modally without
being placed on the desktop deck.  Built by "make" / "make vwmprint"
(needs libcups2-dev); not part of "make core".

Added a "Lock screen" item to the VWM menu (above "Capture
screenshot") that starts the configured screensaver immediately,
without waiting for the idle timeout.  It does nothing if no
screensaver command is configured.

Fixed border corruption when resizing a window with the WM-mode
grow/shrink hotkeys.  The window canvas was resized but the frame was
never re-rendered, so the old border stayed in the top-left and the
newly exposed area was blank.  Windows are now repainted after each
resize, and vwmterm terminals resize their content to match -- the
content widget expands with the frame and the vterm is re-flowed.

Configuration moved from libconfig to JSON.  Settings now live in
$HOME/.config/vwm/config.json (created with sane defaults on first
run) rather than ~/.vwm/vwmrc, organized into three sections:
"hotkeys" (flat name to hex keycode), "programs" (an array of app
entries), and "settings".  The file is parsed and written with a
vendored copy of cJSON, so libconfig is no longer a build dependency.
An existing ~/.vwm/vwmrc is left untouched; convert it by hand using
samples/config.json as a guide.

Added a screen saver.  When the idle timeout set in the Settings
dialog elapses with no keyboard or mouse activity, VWM launches a
configured program (for example vlock) in a full-screen terminal
layered above the desktop and panel.  While it runs, every hotkey and
mouse action is withheld from the window manager and delivered only to
that program; normal operation resumes when the program exits.  The
command accepts arguments, the idle timeout is given in minutes (0
disables the screen saver), and the countdown is suspended while a
menu or dialog is open.  The Settings dialog gained fields for the
screen-saver command and idle timeout.

Fixed heap corruption when closing a menubar dropdown or the calendar
popup.  The child widget was destroyed before the window that owned
it, so the window destructor unlinked an already-freed list node.
Both call sites now detach the child before destroying either widget.

Fixed a crash when the screen saver's terminal closed.  Its window is
attached to the surface rather than a deck, so removing it ran a
list_del on a never-linked node; VDK now initializes every widget's
list node at construction, making the operation safe.

2026-05-28

Added a Settings and Preferences dialog to the VWM menu.  It configures
the panel task-indicator click action, the clock (date) click action,
and the number of desktops (2-6), all persisted to vwmrc.  A change to
the desktop count is applied when the dialog closes, so surfaces are
never torn down while the dialog is still on screen.

Added a Screen Capture tool (the vwmscrshot module) to the VWM menu,
under "Switch desktop".  It asks whether to capture the whole screen
or the top window, renders the ncurses cells to a PNG with FreeType
(alpha-blended glyphs, ACS line/box characters, and bold, underline,
reverse, and dim attributes), and saves through a file picker.

The Manage Apps "Load Config" dialog now has Tab stops: focus cycles
through the file browser, OK, and Cancel, with the focused button
highlighted.

Menubar dropdowns (VWM / Apps) are now bright white on cyan with a
black highlight bar, replacing the previous dim white with a red
highlight.  The Apps menu scrollbar is drawn in black.

Clicking the panel now only triggers the task-indicator action when
the click lands on the task counter itself, not anywhere on the panel.

STYLE_GUIDE.md gained a File Dialogs section (theme, internal layout,
tab stops, and the nested-composer render rule) and its dropdown-menu
colors were updated.

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
