vwm performance review
======================

Findings from a code review focused on per-event and per-frame hot
paths.  Ordered by expected impact within each band.  This file is
local; commit it or not as you prefer.


HIGH IMPACT
-----------

[x] 1. Wallpaper repainted from scratch every screen refresh
       bkgd.c  vwm_bkgd_simple_normal()
       Every vk_screen_refresh() calls the wallpaper callback, which
       walks every cell of the surface (width * height calls to
       mvwadd_wch / waddch / setcchar).  At 200x60 that's ~12000
       per-cell calls per refresh, every refresh.  The wallpaper is
       STATIC -- it only changes when the user changes desktop color
       or wallpaper pattern in Settings, or when the screen resizes.

       DONE: per-surface backing WINDOW cache in bkgd.c.  The wallpaper
       callback fills a NULL slot via newwin + the existing per-pattern
       render helpers, then blits with overwrite().  Invalidations:
         - Settings color or pattern change   (manage_settings.c, only
           when the new value differs from the current one)
         - surface_count shrink                (vwm.c apply_surface_count)
         - geometry mismatch on canvas resize  (lazy, in the callback)
         - teleport                            (vwm.c vwm_on_teleport,
           orphan variant: nulls the slots without delwin since the
           WINDOWs are bound to a SCREEN that's about to die, same
           intentional leak as libviper's canvases)

[ ] 2. classify_mouse is ~370 lines of duplicated popup hit-testing
       poll_input_thd.c lines 92-417 (classify_mouse function)
       Per mouse event (every cursor move while hover-tracking is on),
       we walk through 19+ optional popup pointers, each with the same
       NULL-check + vk_widget_get_position + vk_widget_get_metrics +
       bounds-test boilerplate (about 12 lines per popup).
       manage_apps_popup has 7 sub-popups checked.  manage_hotkeys has
       5.  manage_settings has 7.  Plus top-level menu, calendar, etc.

       Two fixes, either one or both:
       a) Table-driven: array of (popup_getter_fn, zone_id) pairs and a
          single bounds-test loop.  Same behavior, ~10x fewer lines.
       b) Maintain a small "currently visible popups" list that each
          system tool's open/close routines push/pop themselves into.
          classify_mouse then walks 0-3 entries instead of 19.

[ ] 3. vk_screen_refresh fires on every keystroke regardless of state
       change
       poll_input_thd.c (every branch in vwm_poll_input ends with
       vk_screen_refresh).
       Pure text typing into a vwmterm doesn't change any vwm-level
       state -- the keystroke goes via push_keystroke into the vterm,
       which writes to the PTY; the visible change happens later when
       the child writes back and pt_thread renders it.  Yet vwm
       refreshes the whole screen for the keystroke itself.

       Fix: have each branch set a needs_refresh flag and only call
       vk_screen_refresh at the bottom of the loop iteration when it's
       set.  Branches that genuinely don't change visible state (the
       common typing-into-vwmterm path) just skip the refresh.  Cuts
       roughly half the surface composites during heavy typing.


MEDIUM IMPACT
-------------

[ ] 4. Multiple vk_screen_refresh per logical mouse event
       poll_input_thd.c, the KEY_MOUSE branch.
       The "close popup that lost focus" sequence (manage_hotkeys,
       manage_settings, manage_apps, menu, calendar) calls
       vk_screen_refresh inside each close branch -- and the parent
       switch then calls vk_screen_refresh again at the end (line 851).
       Same logical event triggers 2-3 full screen composites.

       Fix: drop the early refreshes; let the trailing one cover them.
       Or set a flag and refresh once.

[ ] 5. pt_thread.c refreshes per drain cycle, not per scheduler tick
       modules/vwmterm3/pt_thread.c lines 100-107
       Every vwmterm thread calls vk_screen_refresh whenever its own
       drain produced bytes.  With multiple busy vwmterms (split-screen
       tail -f or htop in two tiles), each tile's pt_thread does its
       own surface refresh per turn.  N busy tiles = N full screen
       composites per scheduler round.

       Fix: batch.  Either (a) have pt_thread set a global
       redraw_pending and let the scheduler emit one refresh per round
       after all threads have run, or (b) coalesce by checking whether
       any other tile is already redraw_pending and skip if so (the
       last one wins).

[ ] 6. vk_window_set_title called on no-op transitions
       modules/vwmterm3/events.c lines 300, 322, 403
       Selection enter/exit and OSC title changes call
       vk_window_set_title every time, even when the new title equals
       the current one.  vk_window_set_title strdup's the new title
       and triggers a window redraw.

       Fix: compare against current title (via vk_window_get_title)
       and skip if identical.

[ ] 7. Wallpaper cchar_t built per refresh
       bkgd.c  _bkgd_render_small_bricks, _bkgd_render_large_bricks
       Each render builds cchar_t copies from WACS_* sources every
       call.  Resolved automatically if item 1 lands (the cchar_t's
       are constructed during the cache build, not the per-refresh
       blit).  Listed for completeness; don't fix in isolation.

[ ] 8. Panel display redoes vk_widget_set_colors every clock tick
       panel.c  vwm_panel_display() + its callees
       Every clock tick (10x per second per VWM_CLOCK_TICKS_PER_SEC)
       walks the panel widget tree and calls vk_widget_set_colors on
       every box / label / spacer, plus vk_label_update on each.  Most
       of the time nothing has changed.

       Fix: hoist vk_widget_set_colors out to one-time panel init; only
       re-call when a theme actually changes.  vk_label_set_text +
       vk_label_update only when the new text differs from current.


LOWER IMPACT / CLEANUP
----------------------

[ ] 9. Hover/move mouse events are not coalesced
       poll_input_thd.c lines 538-545 (vk_kmio_mouse_drain coalescing
       inside the DRAG branch).
       Drag positions are coalesced down to the latest, but plain
       hover-move events (with hover-tracking on) are not.  Heavy
       cursor movement floods classify_mouse + downstream handlers.

       Fix: extend the coalesce-while-no-button-event loop above the
       drag-mode check so hover floods collapse the same way.

[ ] 10. Per-mouse-event ZONE_PANEL hardcodes my == 0
       poll_input_thd.c line 102
       "if(my == 0) return ZONE_PANEL;" assumes the panel is row 0
       with height 1.  Fast but brittle -- a future taller panel
       breaks silently.  Replace with a stored panel y/height.

[ ] 11. vwm_module_find_by_title is linear strcmp
       modules.c line 345
       Called from poll_input_thd.c's clock/task panel-click branches
       (rare).  Fine as-is unless modules get plentiful.

[ ] 12. vwmterm_copy_selection allocates worst-case buffer up front
       modules/vwmterm3/events.c line 237
       buf_sz = (r2 - r1 + 1) * (cols * MB_LEN_MAX + 1).  For a
       full-screen 200x60 selection that's ~360KB allocated eagerly.
       Single-shot (only on SELECT-mode copy), so impact is small.

       Fix: realloc down at the end, or compute exact length in a
       first pass.

[ ] 13. Repetitive get_X_popup() accessor functions
       manage_apps.c, manage_hotkeys.c, manage_settings.c, etc.
       Each system tool exports half a dozen vwm_X_get_<thing>_popup()
       accessors so classify_mouse can hit-test them.  Solved
       structurally by item 2.  Listed for completeness.


BIGGEST WIN AT LOWEST RISK
--------------------------

  Item 1 (cache the wallpaper).  Changes one file (bkgd.c) plus the
  spots that already update on color/pattern change, has a clean
  invariant ("the cache is correct unless its surface_id's geometry
  or color/pattern changed since"), and removes thousands of per-cell
  ncurses calls from every screen refresh.

  Items 3 and 5 (skip refresh when no state changed; batch pt_thread
  refreshes) compound nicely on top of item 1: fewer refreshes, each
  refresh cheaper.
