vwm - virtual window manager for the terminal
==============================================

vwm runs inside a single terminal and presents a multi-desktop, mouse-aware
window manager with embedded ncurses terminals, dropdown menus, and
dialog-driven configuration.  It builds on libviper (VDK widgets) and
libvterm.

FEATURES
========

*  Multiple virtual desktops, each with its own color and wallpaper pattern
   (Stiple, Small Bricks, Large Bricks, Dots, Crosses).  Switch desktops
   with Alt+d.
*  Embedded terminal windows (vwmterm) with scrollback -- a mouse scrollbar,
   wheel, and Alt+PgUp / Alt+PgDn -- click-drag SELECT-mode copy to the host
   clipboard, child OSC 52 copy (vim, Grok, tmux, …) on the same path, and
   middle-click paste.
*  Menubar with two dropdowns: VWM (system tools) and Apps (user-configured
   launchers, grouped into category submenus that open beside the menu as
   you move over them).  Reach it with the menubar hotkey or mouse.
*  In-app configuration dialogs -- no editor required for common changes:
   -  Settings: per-desktop foreground/background colors and wallpapers,
      screensaver command and idle timeout, copy-to-clipboard transport
      (xclip / host / both), and an optional bottom-left host name with a
      choice of size (basic, or a large Terminus font), fill, and colors.
   -  Manage Apps Menu: add, edit, or hide launcher entries that appear under
      the Apps dropdown; each entry can set its own default terminal size
      (character-cell width × height; default 80×25). Load Config imports a
      JSON profile.
   -  Manage Hotkeys: rebind built-in shortcuts and persist them.
   -  Manage Desktop / Manage Windows: rearrange and move windows across
      desktops.
*  System tools (under the VWM menu):
   -  Capture Screenshot - renders the active ncurses surface to a PNG via
      FreeType (built-in; also available as vwm_screenshot_save()).
   -  Print File - sends a file to a CUPS-discovered printer.
   -  Lock Screen - invokes the screensaver on demand; also fires
      automatically after the configured idle timeout.
   -  Detach - give this terminal back to its shell and leave the
      session running; vwm-resume brings it back.  See MOVING A
      SESSION.
*  Permanent status bar with clock, hotkey hints, version, and a GPM-driven
   mouse cursor overlay.
*  Optional host name in the desktop's bottom-left corner (off by default),
   in a chosen color and -- via the loadable vwmfont module -- a large
   Terminus pixel-font at one of nine grid sizes.
*  Durable: the session runs in the background, apart from the terminal
   it is shown on.  Detach and come back later, close the window, lose
   the SSH connection -- everything in it keeps running, and vwm-resume
   brings it to whichever terminal you are on.
*  A session can move between terminals -- X terminal, SSH login, Linux
   console -- and vwm adapts to each one: terminal type, mouse (xterm
   reporting or GPM), and UTF-8 or ASCII glyphs.
*  Control plane: vwm-msg talks to the running session (list / launch /
   focus / close / …) over a per-user Unix socket.
*  Configuration persisted to JSON at ~/.config/vwm/config.json; a sane
   default is written on first run.

REQUIREMENTS
============

CMake
ncursesw 5.4+
libviper 10.0.0+ - https://github.com/TragicWarrior/libviper
libvterm 10.9+ - https://github.com/TragicWarrior/libvterm
FreeType         (for screen capture; DejaVu Sans Mono is bundled)
                 cmake -DVWM_SCREENSHOT_FONT= / -DVWM_SCREENSHOT_FONT_BOLD=
                 to force a different TTF
libcups2-dev     (for the print module; "make all")
zlib             (for the big-font hostname module, vwmfont)
xclip (optional) - for "xclip" / "Both" Copy-to-Clipboard modes under X
gpm (optional)   - the gpm daemon, at run time, for the mouse on a Linux
                   console; nothing is needed to build

CONTROL SOCKET
==============

The running vwm listens on $VWM_CONTROL_SOCK (default
~/.config/vwm/control.sock).  Only the same uid can connect.  From any
shell -- including one inside a vwmterm -- run vwm-msg:

    vwm-msg ping
    vwm-msg list-windows
    vwm-msg list-apps
    vwm-msg launch-app VTerm Color
    vwm-msg launch --bin /usr/bin/htop
    vwm-msg send-keys <id> --text "ls -la" --enter
    vwm-msg capture <id>
    vwm-msg screenshot --target top
    vwm-msg attention <id>
    vwm-msg close <id>
    vwm-msg detach
    vwm-msg stop
    vwm-msg attach

ping also reports the session's process id and whether a terminal is
attached.  attach brings the session to
the terminal it is run from (or --tty PATH --term TYPE) and then waits
there, as the shell's foreground job, until the session leaves; it is
what vwm-resume runs, and it is refused from a terminal inside the
session.  See vwm-msg --help.

INSTALLATION
============

By default the build installs vwm, vwm-msg and the launchers to
/usr/local/bin and the plugins (shared libraries) to /usr/local/lib/vwm.

For a simple installation:

cmake CMakeLists.txt
make
sudo make install

On Linux the install ends by running ldconfig, so vwm finds a libviper or
libvterm that was installed just before it.  A staged install (DESTDIR)
skips that step.

CONFIGURATION
=============

Most settings are managed from within vwm via the VWM dropdown in the
menubar -- no editing required.  Open Settings to adjust per-desktop colors
and wallpapers, the screensaver program and idle timeout, the
copy-to-clipboard transport, and the optional bottom-left host name.  Open
Manage Apps Menu to add or modify the
launcher entries shown under the Apps dropdown.  Open Manage Hotkeys to
rebind the built-in shortcuts.  Each dialog persists its changes to the
JSON config on Save.

The config file lives at ~/.config/vwm/config.json and is created with sane
defaults on first run.  Hand-editing is still supported -- a sample is
provided at samples/config.json that you can adapt to your binary paths.

STARTING, LEAVING AND COMING BACK
=================================

    vwm          start a session and show it on this terminal
                 (vwm-start is the same thing)
    vwm-resume   bring the running session to this terminal
    vwm-stop     end the session, from any terminal

vwm runs the session in the background, on no terminal at all, and then
attaches the terminal you started it from.  What stays in the foreground
of your shell is a small stand-in that waits for the session to leave;
the session itself does not depend on that terminal, or on any other.

To leave a session running and get your shell back, detach: press Ctrl-\
(rebindable in Manage Hotkeys), choose VWM > Detach, or run vwm-msg
detach from another terminal.  If the terminal simply goes away -- a
dropped SSH connection, a closed window, a logout -- vwm lets go of it
in the same way on its own.  Either way every program in the session
keeps running.

To come back, run vwm-resume on the terminal where you want vwm.  It
works whether or not the session is showing somewhere else: a terminal
it leaves gets its shell back, with a line saying where the session
went.  vwm-resume run from a terminal inside vwm is refused.

To end the session, run vwm-stop from any terminal, attached or not:
every program in it is hung up as if its terminal had closed, and the
terminal vwm was on is put back.

Started from something that is not a terminal (a script, a service),
vwm brings the session up 80x25 with nothing attached, ready for
vwm-resume.  A session is refused, with the reason, before anything is
started when the terminal is too small or another session is running;
a start that fails afterwards is reported on the terminal too.

On Ubuntu you can have the login greeting remind you that a session is
waiting.  Drop a small executable script into /etc/update-motd.d/ -- the
same mechanism that prints the usual load/disk lines -- that emits a line
only while vwm is running:

#!/bin/sh
pgrep -x vwm >/dev/null 2>&1 || exit 0
printf '\n  A vwm session is running -- reconnect with  vwm-resume\n\n'

Save it as e.g. /etc/update-motd.d/99-vwm-session, make it executable, and
it shows on your next login whenever vwm is up (and stays silent otherwise).
Use a lower number prefix to move the line up.

MOVING BETWEEN KINDS OF TERMINAL
================================

vwm-resume tells vwm what the terminal is -- its tty and $TERM -- and vwm
follows it: the right terminal type, the GPM mouse when it is a Linux
console, xterm mouse reporting when it is an X terminal, the glyphs that
terminal can show, and its size.  A session started in an X terminal can
be resumed on a console or over SSH and back again.

COMING FROM DTACH
=================

Before version 8, surviving a disconnect meant running vwm under dtach
with vwm-start.  vwm does that itself now and dtach is not used: plain
vwm is durable, vwm-start is kept as another name for it, and the
status bar no longer carries a dtach indicator.  Two things differ from
a dtach session: a session shows on one terminal at a time (resuming it
elsewhere moves it, it is not mirrored), and the leftover ~/.vwm.sock
and the VWM_SOCK variable mean nothing any more.

Enjoy!
