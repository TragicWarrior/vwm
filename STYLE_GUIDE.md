# VWM UI Style Guide

Color and attribute conventions for dialogs, popups, and controls.
All colors reference ncurses `COLOR_*` constants.

## Main Dialogs

Used for full control panels: Manage Apps Menu, Manage Hotkeys,
Manage Settings, CUPS print picker, Calendar.

| Element              | Foreground | Background | Attrs  |
|----------------------|------------|------------|--------|
| Window border        | WHITE      | CYAN       | A_BOLD |
| Interior / vbox      | BLACK      | CYAN       |        |
| Labels               | BLACK      | CYAN       |        |
| Listbox text                  | BLACK      | CYAN       |        |
| Listbox highlight (focused)   | BLACK      | RED        |        |
| Listbox highlight (unfocused) | BLACK      | WHITE      |        |
| Listbox frame        | (sunken 3D relief — see below) |
| Scroller border      | BLACK      | CYAN       |        |
| Dropdowns            | BLACK      | CYAN       | A_BOLD |
| Dropdown highlight   | CYAN       | BLACK      |        |
| Button (active)      | YELLOW     | CYAN       | A_BOLD |
| Button (inactive)    | BLACK      | CYAN       | A_BOLD |
| Button bar / spacer  | BLACK      | CYAN       |        |

### Picker frame — sunken 3D relief

The four picker-style tools (Manage Apps, Hotkeys, Settings, CUPS
print) wrap their listbox in a `vk_frame_t` drawn with sunken relief.
That shared visual signature is what makes them feel like the same
family of dialog.  Set the border style to
`VK_BORDER_SINGLE | VK_RELIEF_SUNKEN`; the frame then paints its top +
left edges in `widget->relief_lo` (default BLACK) and its bottom +
right edges in `widget->relief_hi` (default WHITE), both on the
border background.  When a relief flag is set, `border_fg` is ignored
— relief colors come exclusively from `relief_hi`/`relief_lo`
(override per-frame with `vk_widget_set_relief_colors()` if the
defaults don't read well against your bg).

```c
listbox_frame = vk_frame_create(W, H);
vk_frame_set_border_style(listbox_frame,
    VK_BORDER_SINGLE | VK_RELIEF_SUNKEN);
vk_frame_set_border_colors(listbox_frame, COLOR_BLACK, COLOR_CYAN);
vk_frame_set_border_attrs(listbox_frame, A_BOLD);
vk_frame_set_child(listbox_frame, VK_WIDGET(listbox));
```

`border_fg` (`COLOR_BLACK` above) is a placeholder; pick any value.
`border_bg` (CYAN) is the actual background each relief cell paints
against.  `A_BOLD` applies to the relief cells.

Focus indication: toggle `border_attrs` between `A_BOLD` (focused)
and `0` (unfocused) — the relief reads as bold-sunken vs normal-
sunken rather than the older yellow-vs-black border-fg swap.  The
`border_fg` toggle in legacy focus-update code is now a no-op but is
harmless to leave in.

### Picker listbox — focus-aware highlight

System-tool listboxes carry **two** highlight pairs so the selected
item visibly tracks focus.  Every standalone listbox in a system tool
(Manage Apps, Hotkeys, Settings, the Print printer-list, and the
filedialog's internal file_list — reach the last via
`vk_filedialog_get_file_list()`) is set up this way:

| State      | Foreground | Background |
|------------|------------|------------|
| focused    | BLACK      | RED        |
| unfocused  | BLACK      | WHITE      |

API (vk_listbox, libviper):

- `vk_listbox_set_highlight(lb, fg, bg)` — focused-state pair.
- `vk_listbox_set_unfocused(lb, fg, bg)` — unfocused-state pair.
- `vk_listbox_set_focused(lb, bool)` — toggles which pair is active.

Recipe at widget setup:

```c
vk_listbox_set_highlight(lb, COLOR_BLACK, COLOR_RED);
vk_listbox_set_unfocused(lb, COLOR_BLACK, COLOR_WHITE);
```

Inside the tool's focus-update function (`update_dropdown_highlights`,
`update_pick_focus`, etc.):

```c
vk_listbox_set_focused(lb, model->focus_zone == FOCUS_LIST);
vk_listbox_update(lb);
```

The default at creation is focused (BLACK/RED), so legacy code that
never calls `set_focused` is unaffected.  When `set_unfocused` is
never called, `set_focused(false)` is a no-op — the active highlight
stays put.

Calendar-specific additions:

| Element              | Foreground | Background | Attrs  |
|----------------------|------------|------------|--------|
| Window border        | BLACK      | CYAN       |        |
| Calendar body        | BLUE       | CYAN       |        |
| Today highlight      | BLACK      | RED        |        |
| Dimmed days          | BLACK      | CYAN       | A_BOLD |
| Header (month/year)  | WHITE      | CYAN       | A_BOLD |

## Warning / Error / Confirm Popups

Used for warnings (resize), errors (duplicate hotkeys), and
discard-confirmation prompts. Red theme signals caution.

| Element              | Foreground | Background | Attrs    |
|----------------------|------------|------------|----------|
| Border               | RED        | WHITE      | A_NORMAL |
| Title                | RED        | WHITE      |          |
| Client area          | RED        | WHITE      |          |
| Labels / fillers     | RED        | WHITE      |          |
| Button bar           | RED        | WHITE      |          |
| Button bar fill      | RED        | WHITE      |          |
| Button (active)      | YELLOW     | WHITE      | A_BOLD   |
| Button (inactive)    | BLACK      | WHITE      | A_BOLD   |

Fill the client area before updating:

```c
vk_widget_fill(VK_WIDGET(client),
    ' ' | COLOR_PAIR(vdk_color_pair(COLOR_RED, COLOR_WHITE)));
vk_box_update(client);
```

## Input / Info Popups

Used for edit forms (Edit App, Add App), file choosers (Load),
and informational messages (Settings saved). Blue theme signals
neutral interaction.

| Element              | Foreground | Background | Attrs  |
|----------------------|------------|------------|--------|
| Border               | WHITE      | BLUE       | A_BOLD |
| Client area          | WHITE      | BLUE       |        |
| Labels               | WHITE      | BLUE       |        |
| Input fields         | BLACK      | BLUE       |        |
| Button bar           | WHITE      | BLUE       |        |
| Button (active)      | YELLOW     | BLUE       | A_BOLD |
| Button (inactive)    | WHITE      | BLUE       | A_BOLD |
| File dialog text     | WHITE      | BLUE       |        |
| File dialog highlight| WHITE      | RED        |        |

Fill the client area before updating:

```c
vk_widget_fill(VK_WIDGET(client),
    ' ' | COLOR_PAIR(vdk_color_pair(COLOR_WHITE, COLOR_BLUE)));
vk_box_update(client);
```

## File Dialogs (Load / Save)

`vk_filedialog_t` is used inside a blue Input/Info popup or window (e.g.
Manage Apps "Load Config", the screenshot "Save Screenshot" picker). It is
a `vk_box` subclass that supplies its own OK/Cancel buttons — do **not**
add popup buttons of your own. Give it the full interior; for a save-style
dialog put a "Save as" `vk_input` above it and let the filedialog expand
to fill the rest.

### Theme (blue)

| Element                | Foreground | Background | Attrs  |
|------------------------|------------|------------|--------|
| Enclosing border               | WHITE      | BLUE       | A_BOLD |
| File list / path text          | WHITE      | BLUE       |        |
| Selection highlight (focused)  | BLACK      | RED        |        |
| Selection highlight (unfocused)| BLACK      | WHITE      |        |
| Okay / Cancel (inactive)       | WHITE      | BLUE       | A_BOLD |
| Okay / Cancel (focused)        | YELLOW     | BLUE       | A_BOLD |

```c
fd = vk_filedialog_create(interior_w, interior_h, VK_BORDER_SINGLE, false);
vk_filedialog_set_colors(fd, COLOR_WHITE, COLOR_BLUE);
vk_filedialog_set_highlight(fd, COLOR_BLACK, COLOR_RED);    /* focused */
vk_listbox_set_unfocused(vk_filedialog_get_file_list(fd),   /* unfocused */
    COLOR_BLACK, COLOR_WHITE);
vk_filedialog_set_button_colors(fd, COLOR_WHITE, COLOR_BLUE);
vk_filedialog_set_button_attrs(fd, A_BOLD);
vk_filedialog_set_filter(fd, "pdf,md,txt");   /* optional ext whitelist */
```

`vk_filedialog_set_highlight()` sets the **focused** pair on the
underlying listbox; for the unfocused pair reach the listbox via the
getter and call `vk_listbox_set_unfocused()`.  The consumer's
focus-update function then toggles via
`vk_listbox_set_focused(vk_filedialog_get_file_list(fd), …)`.

### Built-in sunken-relief frame around the file list

`vk_filedialog_create` automatically wraps its file_list in a
sunken-relief `vk_frame_t` (the same look as the other picker
frames).  Consumers don't have to do anything — the frame is created
inside the filedialog and its colors track `vk_filedialog_set_colors`.
The frame costs the file list **2 rows** vertically and **2 columns**
horizontally (1 each for the border).  Account for this when computing
the popup's height.

### Extension filter

`vk_filedialog_set_filter(fd, "pdf,md,txt")` restricts the file list to
regular files whose extension (substring after the last `.`) matches
one of the comma-separated entries.  Comparison is case-insensitive
and the entries carry no leading dot.  Directories are always shown
regardless of the filter so the user can navigate.  Pass `NULL` or
`""` to clear the filter and show everything.  Calling it on an
already-open dialog refreshes the listing immediately.

### Internal layout (for mouse hit-testing)

Relative to the filedialog's own area: the top 3 rows are the path input,
the bottom 3 rows are the Okay/Cancel button bar, and everything between
is the file list **surrounded by a 1-row sunken-relief frame border**.
Map a list click as `item = scroll_pos + (rel_row - 3 - 1)` — three rows
for the input strip plus one for the frame's top border.  In the button
row, Okay is the left half (`x < interior_w / 2`), Cancel the right half.
A click in the path strip can focus it by pushing `'/'` to the dialog.

Forgetting the `- 1` adjustment for the frame border makes file-list
clicks land one row higher than the cursor; omitting the button-row
branch entirely makes Okay and Cancel unclickable.

### Tab cycling

Four of the five filedialog consumers (Manage Apps, Manage Hotkeys,
Manage Settings — the last with Tab cycling pending — Print File,
Save Screenshot) implement Tab cycling between the filedialog body,
the Okay button, and the Cancel button.  Each tool defines its own
focus enum (`LF_*`, `HL_*`, `PK_*`, `SF_*` respectively) plus an
`update_*_focus()` helper that:

1. Recolors Okay (`COLOR_YELLOW`/`COLOR_WHITE` on `COLOR_BLUE`,
   A_BOLD) and Cancel similarly based on which is focused.
2. Calls `vk_listbox_set_focused(file_list, focus == *_FILEDIALOG)`
   so the file list highlight switches between BLACK/RED and
   BLACK/WHITE.

The keystroke handler then:
- Tab `'\t'` cycles to the next stop and calls `update_*_focus()`.
- Enter / Space when on Okay → invokes the load/save handler.
- Enter / Space when on Cancel → closes the popup.
- All other keys when on the filedialog → pushed via
  `vk_object_push_keystroke(VK_OBJECT(fd), keystroke)`.

### Tab stops / button focus

The Okay/Cancel buttons have no public accessor, but the filedialog is
a `vk_box`, so reach them through the box getter (avoids pulling in
`vk_filedialog.h`, which transitively fails to find `list.h`):

```c
vk_widget_t *bar    = vk_box_get_widget(VK_BOX(fd), 2);  /* slot 2 = bar */
vk_widget_t *ok     = vk_box_get_widget(VK_BOX(bar), 0); /* slot 0 = Okay */
vk_widget_t *cancel = vk_box_get_widget(VK_BOX(bar), 1); /* slot 1 = Cancel */
```

Cycle focus with Tab in visual (top-to-bottom) order — e.g. filename →
browser → Okay → Cancel, or just browser → Okay → Cancel for a load
dialog.  Highlight the focused button and reset the rest:

```c
vk_button_release(VK_BUTTON(ok));
vk_widget_set_colors(ok,
    focus == OK ? COLOR_YELLOW : COLOR_WHITE, COLOR_BLUE);
vk_widget_set_attrs(ok, A_BOLD);
vk_button_update(VK_BUTTON(ok));
```

When the mouse interacts with the list or path, reset focus back to the
browser so a subsequent Tab stays consistent.

### Rendering

The filedialog is a nested box, so it must be re-rendered on its own
*before* the enclosing box/popup blits it (see *Client Area Pattern*).
After any change — navigation, selection, or focus — call
`vk_filedialog_update(fd)` first, or the dialog stays stale/blank and
clicks appear to do nothing:

```c
vk_filedialog_update(fd);     /* re-render the nested filedialog */
vk_box_update(client);        /* parent only copies fd's composer */
vk_popup_update(popup);       /* (or vk_window_update) */
vk_screen_refresh(vwm->screen);
```

## Dropdown Menus (VWM / Apps menubar)

| Element              | Foreground | Background | Attrs  |
|----------------------|------------|------------|--------|
| Window border        | WHITE      | CYAN       | A_BOLD |
| Listbox text         | WHITE      | CYAN       | A_BOLD |
| Listbox highlight    | WHITE      | BLACK      | A_BOLD |
| Scroller border      | BLACK      | CYAN       |        |

## Client Area Pattern

VDK popup client areas require explicit fill and box update before
`vk_popup_update`, otherwise the background renders black. Always
follow this sequence:

```c
vk_widget_fill(VK_WIDGET(client), ' ' | COLOR_PAIR(...));
vk_box_update(client);
vk_popup_update(popup);
vk_screen_refresh(vwm->screen);
```

## VDK Resize Bug Workaround

`_vk_widget_resize` has an off-by-one that drops the rightmost column
on expanded widgets. After `vk_popup_set_client`, clear the expand
flag on the client:

```c
uint32_t st = vk_widget_get_state(VK_WIDGET(client));
vk_widget_set_state(VK_WIDGET(client), st & ~VK_STATE_EXPAND);
```

## Widget Recreation After Resize

`vk_widget_recreate` blanks all canvases. After calling it, every
child widget (labels, buttons, dropdowns) must be explicitly updated
to restore content and colors. Call `refresh_dialog()` after
recreation.

## Managed vs. Unmanaged Windows

VWM draws two kinds of windows:

- **Managed (app) windows** live on the per-desktop `vk_deck_t`
  (`vk_deck_add_widget`). The window manager owns them: they can be
  raised, cycled (Alt+w), moved, resized, and closed, and they cast a
  shadow. App modules (e.g. vwmterm) return their window from
  `module->main()`, and `vwm_menu_helper()` adds it to the deck.

- **Unmanaged (system tool) windows** are *not* on the deck. They attach
  to the active `vk_surface_t` as an overlay (`vk_screen_attach_widget`),
  draw above the deck and the panel, and are not WM-managed (no move /
  resize / cycle / shadow). The Manage Apps / Hotkeys / Settings dialogs,
  the screensaver, and the print tool all work this way.

Only the deck-top window and a few core dialogs receive input, so a
system-tool **module** cannot rely on the deck. Instead it sets
`vwm->tool_window` to its overlay window; while that is non-NULL,
`poll_input` routes every keystroke (including `KEY_MOUSE`) to it,
modally, ahead of the deck. Such a `main()`:

```c
win = build_window(...);
s->surface = vk_screen_get_active_surface(vwm->screen);
vk_screen_attach_widget(vwm->screen, s->surface, VK_WIDGET(win));
vwm->tool_window = win;          /* grab input while open */
return NULL;                     /* so the launcher does not deck us */
```

Detach from the surface and clear `vwm->tool_window` when the tool
closes.
