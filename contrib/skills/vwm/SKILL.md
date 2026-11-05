---
name: vwm
description: >
  Drive the running vwm session from inside a vwm terminal using
  vwm-msg: open terminals, focus/close/move windows, send-keys, capture
  text, and screenshot. Use when the user is in vwm, wants a new
  terminal or window, mentions vwm-msg, or asks to smoke-test / control
  the desktop. Use when a command needs a TTY or the user must type a
  secret (sudo, apt, ssh, gpg).
---

# vwm

You are running inside a vwm terminal. Drive the session yourself with
`vwm-msg`. Do not ask the user to click menus, open terminals, or paste
by hand.

Socket: `$VWM_CONTROL_SOCK` (else `~/.config/vwm/control.sock`; a
session started with `--sid ID` uses `control-ID.sock`). Same uid only.

Each command prints one JSON line. Success has `"ok":true`. Window ids
are process-lifetime integers from `list-windows` or `launch`. Parse
the JSON; do not guess ids.

## Your desktop only

Your terminal's program was started with `VWM_WINDOW` set, and
`vwm-msg` sends it with every request. vwm answers as seen from the
desktop your terminal is on:

- `list-windows` lists only windows on your desktop. Any other id is
  `unknown id`; that is expected, not an error to work around.
- `launch` / `launch-app` open on your desktop, even when the user is
  looking at another one. They are not disturbed.
- `focus` / `restore` raise a window without changing what is on screen.
- `focused` is the top window of your desktop.
- `desktop <n>` is refused, except to bring the user to your own desktop
  while a window there is flashing (`attention`).
- `stop` and `detach` are refused.

Do not unset `VWM_WINDOW` or bypass this to reach other desktops. If a
task needs a window elsewhere, ask the user.

## Discover

```
vwm-msg ping
vwm-msg list-windows
vwm-msg list-desktops
vwm-msg list-apps
vwm-msg focused
```

`list-apps` is the Apps menu. Use one of its titles with `launch-app`;
do not guess titles. Multi-word titles may be passed with or without
quotes; the remaining words are joined (`launch-app VTerm Color` is the
title VTerm Color).

## Windows (any kind)

```
vwm-msg launch-app <Apps-menu title>
vwm-msg launch --bin /bin/bash
    optional: --profile vterm-color --width 80 --height 25 -- ARG...
vwm-msg focus|close|minimize|restore <id>
vwm-msg move <id> [--dx N] [--dy N] [--x N] [--y N]
vwm-msg resize <id> [--dw N] [--dh N] [--width N] [--height N]
vwm-msg desktop <n>          (own desktop only, while flashing)
vwm-msg attention <id>
vwm-msg attention-off [id]
```

To open a terminal, use a terminal app from `list-apps` if the user has
one, or `launch --bin "$SHELL" --profile vterm-color`. Use `data.id`
from the reply. Open as many as the task needs.

`attention` raises the window, brings your desktop on screen, and
flashes its border (red/yellow) about every 1/2 s until the user clicks
or types in it, or you call `attention-off`. Only one window flashes at
a time. Use it for password/OTP prompts so they notice the window.
`launch --attention` starts flashing at spawn.

## Terminals only (`"kind":"vterm"`)

```
vwm-msg send-keys <id> --text "..." [--enter]
vwm-msg send-keys <id> --file PATH [--enter]
vwm-msg send-keys <id> --key NAME [--key NAME]...
vwm-msg send-keys <id> --type --text "..."
vwm-msg capture <id>
vwm-msg capture <id> --scrollback
vwm-msg capture <id> --scrollback 50
vwm-msg capture <id> --path /tmp/cap.txt
```

`--key` names: `Enter` `Tab` `Esc` `Space` `Backspace` `Delete`
`Up` `Down` `Left` `Right` `Home` `End` `PgUp` `PgDn` `F1`–`F12`
`C-c` `^c` `ctrl-c` `M-x` `alt-x`.

Default `--text` is a paste (bracketed if the program asked for it).
Use `--type` only for per-byte typing. To run a command, add `--enter`.

`send-keys` / `capture` fail on non-terminal windows.

After `send-keys`, wait ~200ms (or retry `capture`) before reading
back. That delay is for local echo only. When waiting on the user or
a long command, poll `capture` until the prompt changes or an error
appears. Capture is the current grid; it does not drain the terminal.

Identify windows by `id` from `launch` / `list-windows`, not by title.
Titles change and are not identity.

## Screenshot (painted window, no scrollback)

```
vwm-msg screenshot --target screen|top [--path FILE]
```

No `--path` writes `/tmp/vwm-shot-<uid>-<pid>-<time>.png` and returns
that path. `top` is your desktop's top window, even when it is not on
screen. `screen` works only while your desktop is the one showing.

## How to work

1. `ping` / `list-windows` if you need the current roster.
2. Open a terminal (see above) or pick an existing `id`. Do not type
   into the terminal you are running in.
3. `send-keys` into that id; wait; `capture` to verify.
4. Report each step's id, capture excerpt, and any error.
5. When finished with a window you launched, `close` it. Do not close
   windows you did not open.

Prefer `vwm-msg` over OSC sequences or menu injection.

## The user types a secret

When a command needs a TTY or the user must type a password, OTP, or
other secret (`sudo`, `apt`, `ssh`, `gpg`):

1. A non-interactive probe (`sudo -n`, and similar) is optional and
   one-shot. If it needs a password or a TTY, open a terminal next.
2. `send-keys` the command into it, then `capture`.
3. On a password/OTP prompt: `attention` that id, tell the user, and
   wait. Poll `capture` until the command succeeds or fails. Do not
   `send-keys` while they type.
4. On failure: report the captured line and stop.
