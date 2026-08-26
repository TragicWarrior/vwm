2026-08-22

Manage Desktop -> Move -> Reorient (was Home coordinates) uses the
same fit as restore: pull the window origin on-screen, then shrink
if it still hangs off the right or bottom edge.  That recovers a
clipped window from the picker instead of leaving it stuck, which
is what a hard move to (1, 1) used to do (ncurses mvwin refuses
any destination that would still overflow).

vwm-msg launch and launch-app run that fit after spawn.  vwm-msg
resize no longer honors a width/height that would hang off the
right edge or cover the status row -- it clamps to the remaining
space from the window's current origin.  That was how an agent
could open a window too large to move.

Building this release needs libvterm 10.9+ and libviper 7.2.0+.


2026-08-17

New windows no longer land on the exact same origin as another window
on that desktop: they step +2 columns and +2 rows while they still fit.
Fullscreen is unchanged.

vwm-msg attention <id> (also launch --attention) raises that window and
pulses its border until you click or type in it, or run attention-off.
Use it when a launched terminal is waiting on you (password prompt).

Building this release needs libvterm 10.9+ and libviper 7.2.0+.


2026-08-16

vwm can be driven from a shell (or an agent) with vwm-msg.  The
running session listens on $VWM_CONTROL_SOCK (default
~/.config/vwm/control.sock); only the same uid can connect.  You can
list windows, launch a program, focus, close, minimize, move, resize,
switch desktops, type or paste into a vwmterm, dump its text, and
take a PNG of the screen.  Example:

    vwm-msg ping
    vwm-msg list-apps
    vwm-msg launch-app VTerm Color
    vwm-msg launch --bin /usr/bin/htop
    vwm-msg list-windows
    vwm-msg send-keys <id> --text "ls -la" --enter
    vwm-msg capture <id>
    vwm-msg screenshot --target top --path /tmp/vwm.png
    vwm-msg close <id>

Building this release needs libvterm 10.9+ and libviper 7.2.0+.


2026-08-14

Screen capture is built into vwm.  The old libvwmscrshot.so module is
gone.  Capture Screenshot in the VWM menu works as before (entire
screen or top window, then a save dialog).  Other code can call
vwm_screenshot_save() with a target and a path and get a PNG with no
UI.  After upgrading, delete any leftover libvwmscrshot.so from the
module directory so it is not loaded as a dead plugin.

PNGs are drawn with DejaVu Sans Mono: the system font if present,
otherwise the copy shipped in fonts/.  cmake
-DVWM_SCREENSHOT_FONT=/path.ttf (and _BOLD) forces a different face;
a missing override is an error, not a silent fallback.

Building this release needs libvterm 10.9+ and libviper 6.0.0+.


2026-08-14

Copy from inside a terminal window now reaches the host clipboard.
Programs that yank via OSC 52 -- vim, Grok, Claude Code, tmux with
set-clipboard, and others -- used to have that sequence swallowed.
vwm now takes it, keeps it for middle-click / Alt+Shift+V paste, and
forwards it according to the existing Copy-to-Clipboard setting
(Never / OSC 52 / xclip / Both).  Under xfce4-terminal, xclip or Both
is still what actually lands on X.

Building this release needs libvterm 10.9+ and libviper 6.0.0+.


2026-08-05

Manage Apps can set a default terminal size per app.  The Category row is now
three columns -- Category, Width, and Height -- with spinbuttons for the
character-cell size (default 80x25).  Values are stored as "width" and "height"
on each programs entry and used when a non-fullscreen terminal launches; if the
preferred size would not fit the host screen, the window is clamped down.

Building this release needs libvterm 10.7+ and libviper 6.0.0+.


2026-07-07

Resizing the terminal while the screen saver is up now works.  If you lock the
screen (or the idle saver kicks in) and then reattach a dtach session at a
different size -- or otherwise resize the terminal -- the saver and the locked
program grow/shrink to fill the new screen instead of staying at the old size.
Only the saver overlay is resized; the desktop it hides is never repainted, so
the resize can't expose it.

Building this release needs libvterm 10.7+ and libviper 6.0.0+.


2026-07-07

Copying text from vwmterm scrollback works correctly.  Previously, if you
scrolled back into history and started a selection, the view jumped to the live
screen -- so the copy began on the wrong row and grabbed live-screen text
instead of the history you were looking at.  The view now stays where you
scrolled, the selection anchors on the row you clicked, and the copied text is
the scrolled-back content you selected.

Building this release needs libvterm 10.7+ and libviper 6.0.0+.


2026-07-06

Windows no longer get stranded oversized after the terminal shrinks.  When you
reattach a dtach/abduco session -- or teleport, or restore a minimized window --
onto a terminal smaller than the window, vwm slides the window back to the
top-left corner and, if it still doesn't fit, shrinks it just enough to fit on
screen.  Before, an over-large window kept its size and spilled past the edges.

Building this release needs libvterm 10.4+ and libviper 6.0.0+.


2026-07-01

Windows can be minimized.  Click the down-arrow on a window's title bar (just
left of the close box) to tuck it out of the way; the top panel shows a
"(N) Minimized" button next to VWM that drops down the list of hidden windows,
and clicking one brings it back on top with focus.  The Alt+W window manager
also gains Minimize and Restore buttons that act on whichever windows you have
checked, and marks the rows that are already minimized with the same arrow.

Building this release needs libvterm 10.4+ and libviper 5.5.0+.


2026-07-01

Terminal scrollback now shows short histories too.  Scrolling back through
output that only slightly overflowed the window -- fewer lines than the
terminal is tall, like a quick `ls -l` -- used to do nothing; now the wheel,
Alt+PgUp / Alt+PgDn, and the scrollbar reveal those lines above the live
screen, just like a hardware terminal, all the way to the first line
captured.

Building this release needs libvterm 10.4+ and libviper 5.4.0+.


2026-07-01

Terminal windows now have a scrollbar.  A bordered vwmterm shows a vertical
bar down its right edge whose thumb reflects how much scrollback you have --
full when there is nothing to scroll, shrinking as history builds, and
reaching the top at the oldest line captured.  Drag the thumb, click the
track, or use the wheel and Alt+PgUp / Alt+PgDn as before; either way you
can no longer scroll past real history into blank rows.  Full-screen
programs like vim, less, or myman have no scrollback, so there the bar parks
at a full thumb and scrolling is disabled, and the bar's color follows the
focused window.  The terminal keeps its full size -- the bar takes its own
column, and the frame's size label reports the real terminal dimensions.

Building this release needs libvterm 10.3+ and libviper 5.4.0+.


2026-06-23

vwm can show the machine's host name in the bottom-left corner of the
desktop, one row above the status line.  It is off by default; turn it on
under Settings with "Show Hostname" and choose its colors with "Hostname
Colors".  For something you can read across the room, "Hostname Font"
renders the name as large pixel-art using a Terminus console font at one
of nine grid sizes, with a "Hostname Fill" choice of a solid block, O, or
X -- that path is the new loadable vwmfont module.  Left at Basic, or with
the module not installed, you get a plain one-row label.  No extra font
package is required.

Desktops now control both colors.  The Settings "Desktop Colors" row
picks a foreground (the wallpaper glyph) and a background together from a
two-swatch chooser, and a new "Crosses" pattern joins the wallpapers.
Bright (8-15) colors that used to come out wrong now render correctly.

Managing windows gained two conveniences.  Manage Desktop can act on
several windows at once -- tick them in the list and use Close Selected or
Move Selected -- and a window left off-screen (for example after resuming
a dtach session in a smaller terminal) is pulled back onto the screen so
you can grab its frame again.


2026-06-22

The modal tool dialogs -- Manage Desktop, Print File, Capture Screenshot
and the rest -- no longer close when you click outside them; only their
own buttons or Esc dismiss them.

The file picker's selected-row highlight is now grey instead of red,
matching the one shown by Capture Screenshot and the "%fd" launch token.


2026-06-20

Manage Apps can prompt for a file at launch.  Put the token "%fd"
anywhere in an app's Params and launching it pops up a file browser
(starting in your home directory); the file you pick is substituted for
%fd and the app starts with it, and cancelling the browser calls off the
launch.  The Add/Edit App dialog labels the Params field with the token
as a reminder.

A couple of Manage Apps fixes came with it: clicking a field in the
Add/Edit form now lands on the field you actually clicked (the Params
field used to be unreachable by mouse), and closing the dialog after
changing an app's category now warns before discarding the change, the
same as the other tools.


2026-06-16

Detach/reattach is now robust in the cases that used to come back
half-working.  Reattaching at the same window size, or reconnecting while
a screensaver or lock (e.g. vlock) is running, now re-arms the mouse,
re-hides the cursor and restores the cursor keys with no manual resize --
the re-arm fires on the reattach itself and again when the lock clears.

You can also have the server's login greeting remind you a session is
waiting: a small /etc/update-motd.d/ script can print a "reconnect with
vwm-resume" line while vwm is running (see the README).


2026-06-14

Run vwm on a remote server across disconnects.  vwm can now be detached
and reattached, so a session on a remote box survives an SSH drop and
picks up exactly where you left off.  Two helpers are installed next to
the vwm binary: run vwm-start to begin a session and vwm-resume to
reconnect to it later (detach with Ctrl-\).  vwm-start won't clobber a
session that is already running -- it points you at vwm-resume -- and
offers to clear away a leftover socket from a crashed one.  Reattaching
repaints the screen, restores the mouse (even after a `reset` on the
detached terminal), and keeps the cursor hidden.  Needs the dtach
package installed and libviper >= 5.1.1.

The saved wallpaper now shows immediately at startup.  A non-default
desktop wallpaper used to stay hidden until the first terminal resize,
because the startup paint cached the desktop background before the saved
settings had loaded.  The cache is now refreshed once settings are in,
so the wallpaper you picked is there from the first frame.

2026-06-12

Window titles no longer go blank after an apps reload.  vwmterm windows
launched from user-defined apps held a pointer to the launching module
record; reloading apps (Manage Apps > Save, or VWM > Reload Apps) freed
those records, and the next time the user entered and exited SELECT
mode on such a window its title would read back as a blank gap in the
title bar.  Windows now snapshot their title at launch and own it for
their entire lifetime.

Menubar reordered to "Apps" before "VWM".  The Apps menu is reached
more often than the VWM (file) menu, so it now sits at the left edge
where the menu bar starts.

2026-06-09

Manage Apps wheel scrolling.  The wheel mouse now scrolls the apps
listbox in the Manage Apps dialog -- previously only BUTTON1 clicks
moved the selection, so users on a wheel mouse had no quick way to
page through a long apps list.  The category / terminal / visibility
dropdowns repopulate to match the new selection just like a click,
and pending dropdown edits are committed before the selection moves.

2026-06-08

Mouse over SSH.  Mouse handling was reworked end to end so hover,
drag, and click behave correctly over SSH -- where the terminal sends
SGR mouse reports and there is no GPM.  vk_kmio (libvdk) now decodes
SGR reports itself rather than trusting ncurses' decode, which masked
the motion bit off the button code and surfaced motion as button
releases (broken hover-highlight, windows raised on hover, dragged
windows stuck to the cursor).  vwm also marks its vterms
VTERM_FLAG_EXTMOUSE, so opening a mouse-aware program (mc, vim) inside
a window no longer makes libvterm seize ncurses' mouse and kill all
mouse input.  Needs libvterm >= 10.1 and libvdk >= 5.1.0.

2026-06-04

Performance: wallpaper backing-WINDOW cache.  Each desktop's
wallpaper used to be re-rendered cell-by-cell on every
vk_screen_refresh -- ~12000 per-cell ncurses calls per refresh on a
200x60 surface, with the brick patterns (setcchar / getcchar per
cell) more expensive still.  The wallpaper is static between user
changes, so each desktop now pre-renders into a cached WINDOW once
and the refresh callback blits with overwrite().  Caches invalidate
on Settings color or pattern change, on surface_count shrink, on
geometry mismatch (lazy resize check + explicit invalidation in the
KEY_RESIZE handler), and on teleport (orphan-style: the cached
WINDOWs belong to a dying SCREEN, so they're nulled without delwin --
same intentional leak libviper does for surface canvases).  Per
refresh, wallpaper cost drops from ~1-2 ms to ~100 us; the
compounding win is most visible on burst-refresh paths (myman
scroll, htop frame, drag-resize).

Performance: skip the surface refresh on fall-through keystrokes
that just got pushed into a vwmterm.  poll_input_thd previously
called vk_screen_refresh after every keystroke including the
fall-through case (no menubar / panel / dialog consumed it; no
popup open), even though the keystroke had only been pushed into
the vterm's PTY -- with no visible vwm-level change until the
child's echo comes back through pt_thread.  Dropping that one
refresh removes a full surface composite per shell-typed
character.

Shadow tint fix.  The recent wbkgdset-on-surface-canvas change
(part of the desktop-flicker work) caused window drop shadows to
take on the desktop color: the deck shadow code writes pair-0
cells (vdk_color_init maps white-on-black to pair 0 for
default-pair compatibility), and ncurses fills pair-0 cells with
the destination window's bkgd pair on write.  Fix: stop applying
the bkgd to the surface canvas; keep applying it to stdscr, which
is what actually mattered for the flicker (wrefresh hardware-scroll
optimizations expose cells whose color comes from stdscr's bkgd).
The surface canvas is always fully overpainted before being
overwritten to stdscr, so dropping its bkgd costs nothing visually
and unbreaks the shadows.


2026-06-03

Reduced desktop flicker during heavy vwmterm scrolling (e.g. running
myman).  Each desktop's color is now seated on its surface canvas via
wbkgdset and the same value is pushed down onto stdscr.  stdscr is
what ncurses turns into the byte stream the outer terminal renders,
so when wrefresh emits hardware-scroll / insert-delete-line
optimizations for a heavy redraw, any cells the terminal momentarily
exposes during those operations now take the desktop color rather
than the terminal's default (typically black).  Set automatically at
init, on desktop color change in Settings, on desktop switch, and
re-applied after teleport (which gives us a fresh canvas + stdscr).

The bkgd value uses only COLOR_PAIR with no attribute bits, so the
ncurses bkgd-OR doesn't bleed bold / reverse / etc. into any cell
written to the surface.  Backed by new libviper APIs
vk_screen_set_surface_bkgd and vk_screen_apply_stdscr_bkgd.

A residual artifact remains for wallpapers that use patterned glyphs
(CKBOARD checkerboard, bricks, dots): you can see solid blue
momentarily before the pattern paints over it.  The bkgd can only
match one cell, not a pattern, so there's no clean fix short of
switching the wallpaper to None (solid fill).  Much milder than the
prior black flash.


2026-06-02

New Manage Desktop system tool, reachable from the VWM menu just
under "Manage windows".  Lists every window on the active deck and
exposes three actions:

  - Close Window: destroys the selected window after a red Yes/No
    confirmation (defaults to "No").
  - Move Window: opens a blue picker with "Home coordinates" (move
    to 1,1) plus a "Desktop N" entry for each desktop when more than
    one is configured.  Moving between desktops re-parents the
    window via vk_deck_remove_widget + vk_deck_add_widget.
  - Cancel: dismiss.

Empty deck shows "None" and the actions become no-ops.  Behaves as
an unmanaged tool window (vwm->tool_window pattern), so all
keystrokes including KEY_MOUSE route through it while open;
KEY_RESIZE recreates the dialog so it re-centers.  Backed by new
libviper APIs vk_deck_count and vk_deck_get_widget.

Menu reorg: Teleport moved into the
Lock/Screenshot/Print/Teleport section -- it's a desktop-state
operation, not a window-management one.

STYLE_GUIDE picks up a new "Label Color Pattern" section
documenting the create -> justify -> set_text -> set_colors ->
vk_label_update sequence.  Skipping the final vk_label_update
leaves the label rendered in its initial canvas colors (typically
black on black against a red Warning popup).

(prior 2026-06-02 entry)

SELECT-mode copy now syncs to the host clipboard.  A new "Copy to
Clipboard" row in Settings picks the transport: Never (internal
paste only, the prior behavior), OSC 52 (an escape sequence the
outer terminal emulator forwards to the system clipboard --
honored by xterm with allowWindowOps, kitty, foot, alacritty,
wezterm, iTerm2, and tmux/screen with set-clipboard on), xclip
(forks xclip -selection clipboard; works under X whenever xclip
is installed, silently no-ops otherwise), or Both (the default).
The internal vwm clipboard (Alt+Shift+V paste) is unchanged and
populated regardless of which host transport is selected.
Persisted to ~/.config/vwm/config.json under settings.clipboard.

The Settings dialog's main listbox now scrolls with the mouse
wheel.  The Modify-popup dropdown and the file picker already
honored the wheel; the dialog's own listbox was the odd one out.

2026-06-01

Per-desktop wallpaper.  Each desktop now picks a repeating tile
pattern alongside its color.  Six patterns are exposed: None (solid
fill), Stiple (the existing ACS_CKBOARD checkerboard), Small Bricks
(2x2 tile of alternating BTEE / TTEE), Large Bricks (6x4 tile,
3-wide bricks with horizontal connectors and selective vertical
lines between facing pips), Dots 1 (period everywhere), and Dots 2
(a 2x2 of period and space).  Settings grows one "Desktop N
Wallpaper" row per active surface, paired with the existing color
row.  vwm_bkgd_simple_normal dispatches by surface; if the terminal
lacks UTF-8 support (locale check + TERM=linux), brick patterns
fall back to Stiple at render time -- the saved config is left
unchanged so revisiting a UTF-8 capable terminal restores the
chosen pattern.

Per-desktop colors AND wallpapers now persist to the JSON config
under settings.desktop_colors and settings.desktop_wallpapers
(string arrays, sized to VWM_MAX_DESKTOPS).  Missing or unknown
entries hold whatever vwm_init seeded.

(prior 2026-06-01 entry)

Per-desktop colors.  Settings now exposes one "Desktop N Color" row
per active surface (5 base rows + surface_count dynamic rows).
Choosing a row opens a vk_color picker -- a 2x8 grid of 1x1 colored
cells with single-line dividers, embedded in a 30x10 popup that's
centered horizontally inside the modify popup's client area.  Arrow
keys clamp inside the picker, Tab advances cell-by-cell (cell 15
hands off to Apply, Cancel wraps back to cell 0), Enter applies, Esc
cancels, mouse clicks land on the nearest cell.  On Save, each row's
color writes to vwm->desktop_color[surface_id] and the screen
refreshes.  vwm_bkgd_simple_normal picks each surface's background
from that array; fresh installs get sensible defaults (Blue, Red,
Cyan, Green, Magenta, Yellow).

Three new VDK widgets back this:

  - vk_grid_t -- 2D layout container.  Sister to vk_box.  Per-row
    and per-col natural sizing + expand bits.  Each cell can hold a
    slot widget (vk_box-style composition) OR be a paint area the
    consumer renders into directly.

  - vk_table_t -- vk_grid + divider rendering.  Single/double/ASCII
    line styles; the gap channel vk_grid reserves becomes the
    divider track.

  - vk_color_t -- 16-cell ANSI color picker.  vk_table subclass.
    cols * rows must equal 16; the focused cell's surrounding
    dividers re-render in a configurable highlight color.

2026-05-31

System-tool listboxes (Manage Apps, Hotkeys, Settings, the Print File
picker, the Save Screenshot picker, and the file_list inside every
filedialog) now use a focus-aware selection highlight: BLACK on RED
when the listbox has focus, BLACK on WHITE when focus has moved
elsewhere (typically to an Okay or Cancel button via Tab).  Two new
libviper APIs back this -- vk_listbox_set_unfocused() registers the
unfocused-state pair, and vk_listbox_set_focused() toggles which pair
is active.  Default at creation is focused, so widgets that don't
need the dance behave as before.

vk_filedialog's file listing area is now automatically wrapped in a
sunken-relief frame for visual consistency with the rest of the
picker tools (Manage Apps/Hotkeys/Settings, the CUPS printer list).
All five filedialog consumers pick this up without code changes; it
costs the visible listing 2 rows and 2 columns.  The mouse-click
list-row calculation was adjusted accordingly (-1 extra for the new
top border).

vk_filedialog gained vk_filedialog_set_filter(fd, "pdf,md,txt") --
an extension whitelist that restricts the file list to regular files
whose extension matches one of a comma-separated list.  Comparison
is case-insensitive, directories are always shown (so navigation
still works), and passing NULL or "" clears the filter.  The Print
File picker uses it to limit the view to .pdf, .md, and .txt.

The Print File picker gained Tab cycling: Tab moves focus between
the file browser, the Okay button, and the Cancel button.  Enter/
Space activates whichever has focus.  Mirrors the existing pattern
in Manage Apps and the Save Screenshot picker.

The Manage Hotkeys Load Config dialog also gained Tab cycling
(filedialog -> Okay -> Cancel) and double-click-to-navigate on the
file list (matching the Save and Settings load dialogs).

2026-05-30

The four picker-style tools -- Manage Apps Menu, Manage Hotkeys,
Manage Settings, and the CUPS Print File picker -- now share a
unified sunken-cyan look.  The listbox sits inside a vk_frame_t
drawn with sunken 3D relief (top + left edges black, bottom + right
edges white, both on cyan; A_BOLD), and the listbox itself is
black-on-cyan instead of the older white-on-black.  Focus on the
frame still toggles A_BOLD on the relief so it's visually
distinguishable from an unfocused picker.  STYLE_GUIDE.md updated
with a new "Picker frame -- sunken 3D relief" subsection.

File dialogs (the Load picker in Manage Apps, Hotkeys, and Settings)
now label the confirm button "Okay" instead of "OK".

Fixed the Load dialog in the Settings tool: the Okay and Cancel
buttons at the bottom would not respond to mouse clicks, and clicks
on the file list landed on the wrong row (off by the input strip's
height).  The mouse handler now mirrors the Manage Apps / Hotkeys
load handler -- input-row click focuses the dialog, button-row
click dispatches Okay/Cancel by left/right half, and the file-list
y-coordinate is corrected by subtracting the input-row height.

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
