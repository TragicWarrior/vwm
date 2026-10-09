vwm TODO
========

One list of open work.  Finished and obsolete items are removed, not
ticked; the CHANGELOG is the record of what was done.

Last pruned 2026-10-09 against 8.0.0.  Items are located by file and
function rather than line number, which drifts.  The IDs (D4, S6, ...)
are kept from the reviews the items came from, so old notes and commit
messages that cite them still resolve; gaps in the numbering are items
that were done or no longer apply.

Each item says where it came from:

    review 2026-06     the performance and simplification reviews
    valgrind 2026-06   valgrind --leak-check=full, 2026-06-17
    review 2026-07     the four-agent review of 2026-07-02

Marked "(not re-checked)" where the 2026-10-09 prune did not confirm the
item by reading the code; everything else was confirmed still present.


1. DEFECTS -- DO FIRST
----------------------

[ ] D14. Big-font hostname cache is not orphaned when the screen is
         rebuilt  (review 2026-07)
         bkgd.c, the static `cached` widget in the big-font hostname
         renderer; vwm.c vwm_on_teleport
         vwm_on_teleport orphans only the wallpaper cache.  The
         hostname-font cache holds a vk_widget whose WINDOW belongs to
         the old SCREEN, so the first font / fill / host change after a
         rebuild calls g_font_free() on a WINDOW bound to a dead SCREEN
         -- the corruption the wallpaper orphan path exists to avoid.
         MORE EXPOSED SINCE 8.0.0: the screen is rebuilt on every detach
         and every vwm-resume, not only on a rare Teleport.  Needs
         vwmfont and the big-font hostname switched on.
         Fix: a hostname-cache orphan hook, called from vwm_on_teleport.

[ ] D2.  Settings "Load" leaves model->selected stale: keyboard Modify
         edits the WRONG setting and saves it  (review 2026-07)
         manage_settings.c load_popup_ok
         Resets the listbox cursor to 0 but not model->selected
         (manage_apps and manage_hotkeys both reset it).  Load a file
         after selecting row 5, press Enter: setting 5 is edited while
         row 0 is highlighted.
         Fix: model->selected = 0 before vk_listbox_set_curr(.., 0).

[ ] D4.  Confirm-Close popup ignores the click row: any click in its
         left half closes the checked windows  (review 2026-07)
         manage_windows.c, the confirm popup's mouse handler
         ((void)rel_y)
         The title, both message rows and the padding all count as
         "Yes".  The move popup and the dialog bars gate on rel_y; this
         one does not.
         Fix: gate Yes/No on rel_y >= ph - 3.

[ ] D1.  "(N) Minimized" dropdown binds raw window pointers
         (use-after-free)  (review 2026-07)
         mainmenu.c create_windows_dropdown / vwm_restore_minimized
         Rows carry the vk_deck_get_widget() pointer.  A minimized
         terminal's program can exit while the dropdown is open; the
         count label is refreshed but the open dropdown is not rebuilt,
         so clicking the row passes freed memory to vwm_restore_window.
         Fix: close or rebuild an open dropdown on window teardown, or
         bind a window id and re-resolve it against the deck on click.

[ ] D13. Closing a window does not cancel a drag that targets it
         (use-after-free)  (review 2026-07)
         winman.c vwm_default_WINDOW_CLOSE, vwm_minimize_window
         A non-mouse key mid-drag falls through, and the close hotkey
         closes the deck top, which is the drag target.  The static
         drag_widget dangles and the next mouse event moves freed
         memory.  Terminals are safe only because vwmterm's ON_CLOSE
         calls vwm_cancel_drag_for_widget itself.
         Fix: call vwm_cancel_drag_for_widget(widget) at the top of both
         functions.

[ ] D10. Manage Desktop acts on the wrong window after another window
         closes under it  (review 2026-07)
         manage_windows.c collect_checked
         Checked row i is mapped to vk_deck_get_widget(deck, i), but a
         window closing on its own shifts every higher index down and
         nothing rebuilds the list while the dialog is open.  Close then
         lands on the wrong terminal.
         Fix: rebuild the list when the deck changes while the tool is
         open, or bind window ids and validate them on use.

[ ] D15. copy_selection writes through an unchecked calloc
         (review 2026-07)
         modules/vwmterm3/events.c vwmterm_copy_selection
         buf_sz = rows * (cols * MB_LEN_MAX + 1), hundreds of KB for a
         full-screen selection, and the result is used with no NULL
         check.
         Fix: NULL-check buf (and free the cell rows) before the fill
         loop.  Item 12 below is the same buffer seen as a cost.


2. DEFECTS -- MEDIUM
--------------------

[ ] D6.  Settings "Load" ignores per-desktop colours and wallpapers; a
         following Save overwrites the file's values  (review 2026-07)
         manage_settings.c model_load_from_config
         Never reads desktop_colors / desktop_fgs / desktop_wallpapers
         (which settings.c writes), so after Load those rows still show
         the running values and Save puts them back.
         Fix: parse the three desktop_* arrays, mirroring
         model_load_from_vwm.

[ ] D7.  Settings Left/Right on the Clipboard and Desktop-Wallpaper
         rows does nothing but still marks the dialog dirty
         (review 2026-07)
         manage_settings.c cycle_value
         No branch for SETTING_CLIPBOARD or the wallpaper rows, so the
         value is unchanged and a bogus "Discard changes?" follows.
         Fix: add the cycle branches, or set dirty only on a change.

[ ] D11. Open manage_* dialogs do not capture mouse clicks that miss
         them  (review 2026-07)
         poll_input_thd.c, the mouse path after classify_mouse
         A click beside an open Settings / Apps / Hotkeys dialog falls
         through to the deck and raises and feeds a terminal behind it.
         The keystroke path guards this (vwm->tool_window); the mouse
         path does not.
         Fix: when a manage dialog is open and the click missed it,
         swallow it or route it to the dialog.

[ ] D5.  manage_apps and manage_hotkeys: the Save / Load / Close hit
         zones are one column left of the buttons
         (review 2026-07, not re-checked)
         manage_apps.c, manage_hotkeys.c mouse handlers
         Clicking Load's right edge triggers Close, and Save's right
         edge opens Load.  manage_settings.c has the same layout right.
         Fix: shift the right-hand cluster's ranges by +1 in both.

[ ] D8.  Manage Desktop leaks its listbox scroller on every close
         (review 2026-07)
         manage_windows.c close path (listbox_scroller = NULL)
         An attached scroller is not owned by its host widget.
         Fix: detach and vk_scroller_destroy before vk_window_destroy.

[ ] D9.  Apps dropdown leaks its scroller on every menu close
         (review 2026-07)
         mainmenu.c create_apps_dropdown / vwm_menubar_close_dropdown
         Fix: detach and destroy the listbox's vscroller in
         close_dropdown.

[ ] D12. Title-bar [v] / [X] hit zones underflow on windows narrower
         than 9 columns  (review 2026-07)
         poll_input_thd.c (rx >= ww - 5 ... / ww - 8 ...) and the
         decorator in private.c
         Windows resize down to width 3; at 5-8 a click on the top-left
         corner minimizes or closes the window.
         Fix: draw and test the controls only when ww >= 9.


3. DEFECTS -- LOW
-----------------

[ ] D16. Menubar width is a constant; a 3-digit "(100) Minimized" label
         would be clipped  (review 2026-07)
         mainmenu.c (menubar_width)
         Not reachable in realistic use.  Fix: derive the width from the
         rendered labels.

[ ] D17. `shutdown` is a plain int written from the SIGTERM handler
         (review 2026-07)
         vwm.c, signals.c vwm_SIGTERM
         Should be volatile sig_atomic_t.  Works today.

[ ] D18. Stray semicolon makes the dup2 unconditional in the crash
         handler  (review 2026-07)
         signals.c vwm_backtrace: `if(fd != -1);`
         _DEBUG builds only; harmless (EBADF).  Fix: remove the `;`.

[ ] D19. isdigit() on an unfiltered key code is undefined behaviour
         (review 2026-07)
         manage_settings.c, numeric modify-input path
         KEY_UP, KEY_MOUSE and the like are above 255.  Benign on glibc.
         Fix: keystroke >= '0' && keystroke <= '9'.


4. MEMORY (valgrind)
--------------------

The numbers are from 2026-06-17 and predate the event-loop scheduler
and the background session.  Re-run before acting on any of these.

[ ] M2.  vwmterm reads an uninitialised value  (valgrind 2026-06)
         "Conditional jump or move depends on uninitialised value(s)"
         in the terminal task (modules/vwmterm3).  Re-run with
         --track-origins=yes to find the origin.

[ ] M3.  Leaks at exit: 4,249 bytes definitely lost in 20 blocks
         (valgrind 2026-06)
         Top sites were dialog and menu open paths, terminal
         allocations and vwm_programs_load.  The OS reclaims them; a
         teardown pass would zero the count.  D8 and D9 are two of the
         causes.

[ ] M1.  ncurses colour-tree "Invalid read of size 4" at startup and
         exit  (valgrind 2026-06)
         Inside libncursesw (tsearch / tdelete), reached through
         libviper's extended-colour setup.  Not fixable in vwm: see
         whether vdk_color_init can avoid it, else suppress.


5. PERFORMANCE
--------------

[ ] 6.   vk_window_set_title is called when the title has not changed
         (review 2026-06)
         modules/vwmterm3/events.c (selection enter / exit, OSC title)
         Each call strdups and redraws the window.
         Fix: compare with vk_window_get_title and skip when equal.

[ ] 9.   Hover-move mouse events are not coalesced  (review 2026-06)
         poll_input_thd.c
         Drag positions collapse to the latest one (vk_kmio_mouse_drain)
         but plain hover moves do not, so heavy pointer movement floods
         classify_mouse.
         Fix: run the same coalescing above the drag-mode check.

[ ] 12.  vwmterm_copy_selection allocates the worst case up front
         (review 2026-06)
         modules/vwmterm3/events.c
         About 360KB for a 200x60 selection.  One-shot, so the cost is
         small.  Fix: measure in a first pass, or realloc down.  See D15.

[ ] E1.  Reducing the desktop count is quadratic in window updates
         (review 2026-07)
         winman.c vwm_on_deck_finalize / vwm.c vwm_apply_surface_count
         Every remove / add during the bulk move fires ON_FINALIZE, so
         moving M windows onto a K-window deck costs O(M * (M + K)) full
         window updates, all discarded before the one refresh.  Cold
         path.  Fix: suppress per-change finalize during the move, then
         finalize once.

[ ] 10.  ZONE_PANEL is hard-coded as row 0  (review 2026-06)
         poll_input_thd.c classify_mouse: `if(my == 0)`
         Fast but brittle: a taller panel would break silently.
         Fix: store the panel's y and height.

[ ] 11.  vwm_module_find_by_title is a linear strcmp  (review 2026-06)
         modules.c
         Called rarely.  Fine unless modules become plentiful.  See S10.


6. SIMPLIFICATION
-----------------

Line counts are rough estimates from the original pass.

[ ] S13. vwm_on_surface_change hand-rolls the deck-finalize loop
         (review 2026-07)
         vwm.c; replace with vk_deck_finalize(vwm->deck).
[ ] S14. Dead free(mod) on an always-NULL local in the module-init
         error path  (review 2026-07)
         modules.c; delete it and the unused local.
[ ] S15. Four identical "Alt+%c" branches collapse to one else
         (review 2026-07)
         manage_hotkeys.c
[ ] S3.  manage_settings: popup-lifecycle trios (~90 lines), two-button
         handler (~55), TASK / DATE actions x3 (~45).  (review 2026-06)
[ ] S4.  manage_hotkeys: an offsetof table for the 14-field
         load / apply / has_changes triplication (~40); scroll twins
         (~22).  (review 2026-06)
[ ] S5.  manage_apps: KEY_UP / KEY_DOWN nav duplicated; dropdown-zone
         table; a dead include and dead output params (~40).
         (review 2026-06)
[ ] S6.  winman: WINDOW_MOVE_* / WINDOW_RESIZE_* siblings -> two bodies
         (~48).  (review 2026-06)
[ ] S7.  panel: message-list scan x4 -> two finders (~30).
         (review 2026-06)
[ ] S8.  bkgd: three fill-pattern helpers -> one (~20).
         (review 2026-06)
[ ] S9.  mainmenu: dropdown boilerplate and nav duplication.
         (review 2026-06)
[ ] S10. modules: find_by_name / title / type share a structure; dead
         ghost declarations.  (review 2026-06)
[ ] S12. vwmterm3: the wheel-down, Alt+PgDn and drain-task render
         blocks are still inline (wheel-up, Alt+PgUp and scrollbar drag
         already share vwmterm_scroll_render); 6-block module
         registration -> a table (~50); selection-normalization
         duplicated (~13).  (review 2026-06)
[ ] 13.  The manage_* dialogs each export half a dozen
         vwm_X_get_<thing>_popup() accessors so classify_mouse can
         hit-test them.  Goes away if popups register themselves.
         (review 2026-06)

Decided against (would trade simplicity for a negligible gain):
  - a task counter in sched.c: a field and an invariant to keep, for
    nothing at 20 tasks
  - restructuring modules.c find_by_type's first iteration: touches the
    iteration contract


7. KNOWN ISSUES OUTSIDE VWM
---------------------------

Carried over from the old BUGS file.  Not re-tested; the report is many
years old and may no longer hold.

  - Under VTE-based terminals (GNOME Terminal and relatives) the screen
    could show stray characters when a character was placed in the
    right-most column.  It was reported upstream as a libvte bug and was
    reproducible with other curses programs.  xterm, rxvt and the Linux
    console were unaffected.
