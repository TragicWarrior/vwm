# VWM UI Style Guide

Color and attribute conventions for dialogs, popups, and controls.
All colors reference ncurses `COLOR_*` constants.

## Main Dialogs

Used for full control panels: Manage Apps Menu, Manage Hotkeys, Calendar.

| Element              | Foreground | Background | Attrs  |
|----------------------|------------|------------|--------|
| Window border        | WHITE      | CYAN       | A_BOLD |
| Interior / vbox      | BLACK      | CYAN       |        |
| Labels               | BLACK      | CYAN       |        |
| Listbox text         | WHITE      | BLACK      |        |
| Listbox highlight    | WHITE      | RED        |        |
| Listbox frame (focus)| YELLOW     | CYAN       |        |
| Listbox frame        | BLACK      | CYAN       |        |
| Scroller border      | BLACK      | CYAN       |        |
| Dropdowns            | BLACK      | CYAN       | A_BOLD |
| Dropdown highlight   | CYAN       | BLACK      |        |
| Button (active)      | YELLOW     | CYAN       | A_BOLD |
| Button (inactive)    | BLACK      | CYAN       | A_BOLD |
| Button bar / spacer  | BLACK      | CYAN       |        |

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

## Dropdown Menus (VWM / Apps menubar)

| Element              | Foreground | Background | Attrs  |
|----------------------|------------|------------|--------|
| Listbox highlight    | WHITE      | RED        | A_BOLD |
| Scroller border      | RED        | WHITE      | A_BOLD |

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
