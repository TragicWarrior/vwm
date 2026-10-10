vwm TODO
========

One list of open work.  Finished and obsolete items are removed, not
ticked; the CHANGELOG is the record of what was done.

Last pruned 2026-10-09 against 8.1.6.  Items are located by file and
function rather than line number, which drifts.  The IDs (D4, S6, ...)
are kept from the reviews the items came from, so old notes and commit
messages that cite them still resolve; gaps in the numbering are items
that were done or no longer apply.

Each item says where it came from:

    review 2026-06     the performance and simplification reviews
    valgrind 2026-06   valgrind --leak-check=full, 2026-06-17
    review 2026-07     the four-agent review of 2026-07-02
    noticed 2026-10    seen while fixing other items, October 2026

Marked "(not re-checked)" where the 2026-10-09 prune did not confirm the
item by reading the code; everything else was confirmed still present.


1. DEFECTS
----------

Every defect from the 2026-07 review has been fixed or found no longer
to apply (8.0.1 through 8.1.3).  The items below were noticed since and
have not been reproduced under a test.

[ ] D20. The move popup's button-row test may be off by one
         (noticed 2026-10)
         manage_windows.c: `if(rel_y >= ph - 3)`
         The same test, copied to the confirm popup, was a row out and
         was corrected there to `rel_y < ph - 4 || rel_y > ph - 2`
         (8.0.3).  The original was not checked against a click.
         Fix: click each row of the move popup and correct the bounds,
         or move it onto vwm_button_bar_hit.

[ ] D21. Manage Desktop does not redraw its list when a window closes
         behind it  (noticed 2026-10)
         manage_windows.c
         The list resolves windows by id (8.1.3), so a stale row is
         harmless to act on, but it stays on screen until the next key.
         Fix: refresh the list when a window is destroyed while the
         dialog is open.

[ ] D22. The status bar keeps a dialog's key help after the dialog
         closes  (noticed 2026-10)
         Fix: restore the default help text on every close path, in one
         place (vwm_popup_dismiss is the natural spot).


2. MEMORY
---------

The leaks found by valgrind in June and re-measured with LeakSanitizer
in October are fixed (8.0.5, 8.1.0, 8.1.2, libviper 10, libvterm
10.10.2).  Opening and closing dialogs and terminals now leaves nothing
behind.  What vwm still holds when it exits is left to the OS.

[ ] M1.  ncurses colour-tree "Invalid read of size 4" at startup and
         exit  (valgrind 2026-06)
         Inside libncursesw (tsearch / tdelete), reached through
         libviper's extended-colour setup.  Not fixable in vwm: see
         whether vdk_color_init can avoid it, else suppress.


3. PERFORMANCE
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
         small, and the allocation is now checked (8.0.4).  Fix: measure
         in a first pass, or realloc down.

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
         Called rarely.  Fine unless modules become plentiful.


4. SIMPLIFICATION
-----------------

Line counts are rough estimates from the original pass.

[ ] S16. Teardown code made redundant by libviper 10  (noticed 2026-10)
         mainmenu.c (dropdown close paths, vwm_menu_free_scroller),
         modules/vwmterm3 (scroller teardown), panel.c (calendar popup
         close)
         A container now destroys its children and a host its attached
         scroller, so these hand-written child teardowns can shrink to
         one destroy of the outermost widget.  Check each under ASan:
         destroying a parent and then its child is a double free.
[ ] S4.  manage_hotkeys: an offsetof table for the 14-field
         load / apply / has_changes triplication (~40); scroll twins
         (~22).  (review 2026-06)
[ ] S5.  manage_apps: KEY_UP / KEY_DOWN nav duplicated; dropdown-zone
         table; a dead include and dead output params (~40).
         (review 2026-06)
[ ] S6.  winman: WINDOW_MOVE_* / WINDOW_RESIZE_* siblings -> two bodies
         (~48).  (review 2026-06)
[ ] S9.  mainmenu: dropdown boilerplate and nav duplication.
         (review 2026-06)
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


5. KNOWN ISSUES OUTSIDE VWM
---------------------------

Carried over from the old BUGS file.  Not re-tested; the report is many
years old and may no longer hold.

  - Under VTE-based terminals (GNOME Terminal and relatives) the screen
    could show stray characters when a character was placed in the
    right-most column.  It was reported upstream as a libvte bug and was
    reproducible with other curses programs.  xterm, rxvt and the Linux
    console were unaffected.
