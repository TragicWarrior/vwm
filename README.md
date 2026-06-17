vwm - virtual window manager for the terminal
==============================================

vwm runs inside a single terminal and presents a multi-desktop, mouse-aware
window manager with embedded ncurses terminals, dropdown menus, and
dialog-driven configuration.  It builds on libviper (VDK widgets) and
libvterm.

FEATURES
========

*  Multiple virtual desktops, each with its own color and wallpaper pattern
   (Stiple, Small Bricks, Large Bricks, Dots).  Switch desktops with Alt+d.
*  Embedded terminal windows (vwmterm) with scrollback (Alt+PgUp / Alt+PgDn),
   click-drag SELECT-mode copy to the host clipboard, and middle-click paste.
*  Menubar with two dropdowns: VWM (system tools) and Apps (user-configured
   launchers).  Reach it with the menubar hotkey or mouse.
*  In-app configuration dialogs -- no editor required for common changes:
   -  Settings: per-desktop colors and wallpapers, screensaver command and
      idle timeout, copy-to-clipboard transport (xclip / host / both).
   -  Manage Apps Menu: add, edit, or hide launcher entries that appear under
      the Apps dropdown; Load Config imports a JSON profile.
   -  Manage Hotkeys: rebind built-in shortcuts and persist them.
   -  Manage Desktop / Manage Windows: rearrange and move windows across
      desktops.
*  System tools (under the VWM menu):
   -  Capture Screenshot - renders the active ncurses surface to a PNG via
      FreeType.
   -  Print File - sends a file to a CUPS-discovered printer.
   -  Lock Screen - invokes the screensaver on demand; also fires
      automatically after the configured idle timeout.
   -  Teleport - migrate the active session to a different PTY without
      restarting.
*  Permanent status bar with clock, hotkey hints, version, and a GPM-driven
   mouse cursor overlay.
*  Remote-friendly: run under dtach via the bundled vwm-start / vwm-resume
   launchers to detach and reattach a session across SSH disconnects.
*  Configuration persisted to JSON at ~/.config/vwm/config.json; a sane
   default is written on first run.

REQUIREMENTS
============

CMake
ncursesw 5.4+
libviper 5.1.1+  - https://github.com/TragicWarrior/libviper
libgpm (optional)
libvterm 10.0+ - https://github.com/TragicWarrior/libvterm
FreeType         (for the screen-capture module; "make all")
libcups2-dev     (for the print module; "make all")
xclip (optional) - for "xclip" / "Both" Copy-to-Clipboard modes under X
dtach (optional) - for remote detach/reattach (vwm-start / vwm-resume)

INSTALLATION
============

By default, build system tries to install plugins (shared libraries) in the
/usr/local/lib/ directory.  

For a simple installation run the following make commands as root:

cmake CMakeList.txt
make
sudo make install

CONFIGURATION
=============

Most settings are managed from within vwm via the VWM dropdown in the
menubar -- no editing required.  Open Settings to adjust per-desktop colors
and wallpapers, the screensaver program and idle timeout, and the
copy-to-clipboard transport.  Open Manage Apps Menu to add or modify the
launcher entries shown under the Apps dropdown.  Open Manage Hotkeys to
rebind the built-in shortcuts.  Each dialog persists its changes to the
JSON config on Save.

The config file lives at ~/.config/vwm/config.json and is created with sane
defaults on first run.  Hand-editing is still supported -- a sample is
provided at samples/config.json that you can adapt to your binary paths.

REMOTE SESSIONS
===============

To run vwm on a remote server and survive SSH disconnects, install the
dtach package and use the bundled launchers instead of calling vwm directly:

vwm-start   - start a new detached session
vwm-resume  - reattach to a running session

Detach at any time with Ctrl-\ ; vwm keeps running on the server.  Log back
in and run vwm-resume to pick up where you left off.  vwm-start refuses to
start over a session that is already running (use vwm-resume instead) and
offers to clean up a leftover socket from a crashed one.  The socket
defaults to ~/.vwm.sock; override it with VWM_SOCK to run more than one.

On Ubuntu you can also have the login greeting remind you that a session is
waiting.  Drop a small executable script into /etc/update-motd.d/ -- the
same mechanism that prints the usual load/disk lines -- that emits a line
only while vwm is running:

#!/bin/sh
pgrep -x vwm >/dev/null 2>&1 || exit 0
printf '\n  A vwm session is running -- reconnect with  vwm-resume\n\n'

Save it as e.g. /etc/update-motd.d/99-vwm-session, make it executable, and
it shows on your next login whenever vwm is up (and stays silent otherwise).
These scripts run as root, so pgrep keeps it simpler than chasing a
per-user ~/.vwm.sock path; use a lower number prefix to move the line up.

Enjoy!
