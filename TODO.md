vwm review backlog
==================

A running backlog from several reviews of vwm, in three parts:

  * PERFORMANCE -- per-event / per-frame hot-path findings, ordered by
    expected impact (the numbered items under HIGH / MEDIUM / LOWER).
  * MEMORY CORRECTNESS -- valgrind findings (the M-items).
  * SIMPLIFICATION / DEDUP -- the tiered code-quality cleanup (S-items).

This file is local; commit it or not as you prefer.


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

[x] 2. classify_mouse is ~370 lines of duplicated popup hit-testing
       poll_input_thd.c from line 94 (classify_mouse function)
       Per mouse event (every cursor move while hover-tracking is on),
       we walk through 19+ optional popup pointers, each with the same
       NULL-check + vk_widget_get_position + vk_widget_get_metrics +
       bounds-test boilerplate (about 12 lines per popup).
       manage_apps_popup has 7 sub-popups checked.  manage_hotkeys has
       6 (saved_popup added in the hotkeys/settings consistency pass).
       manage_settings has 7.  Plus top-level menu, calendar, etc.

       Two fixes, either one or both:
       a) Table-driven: array of (popup_getter_fn, zone_id) pairs and a
          single bounds-test loop.  Same behavior, ~10x fewer lines.
       b) Maintain a small "currently visible popups" list that each
          system tool's open/close routines push/pop themselves into.
          classify_mouse then walks 0-3 entries instead of 19.

       DONE (approach a): a _mouse_hit() helper + a local MHIT() macro
       collapse each candidate's bounds-test to one line; per-dialog
       gates, check order, and zone returns preserved.  ~234 lines
       removed.  Verified with a temporary shadow-compare (the new table
       ran beside the old chain, divergences logged -- none) across a
       full mouse exercise.  NOTE: this kept the get_X_popup accessors
       (still called via MHIT), so item 13 stays open -- it needs
       approach (b) or the manage_ui_common refactor (S2).

[x] 3. vk_screen_refresh fires on every keystroke regardless of state
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

       DONE (already handled, and narrower than the proposed flag
       refactor): the dominant case -- a keystroke falling through to
       the deck-top widget (a vwmterm) -- already skips the refresh.
       That branch in vwm_poll_input pushes the key to the PTY and lets
       pt_thread paint the child's echo (see the comment there).  Every
       other refresh branch handles input that DOES change visible state
       (tool window, resize, dialog/popup/menu/panel keystrokes, mouse
       actions), so a needs_refresh flag would save nothing there.  Goal
       met; no further work.


MEDIUM IMPACT
-------------

[x] 4. Multiple vk_screen_refresh per logical mouse event
       poll_input_thd.c, the KEY_MOUSE branch.
       The "close popup that lost focus" sequence (manage_hotkeys,
       manage_settings, manage_apps, menu, calendar) calls
       vk_screen_refresh inside each close branch -- and the parent
       switch then calls vk_screen_refresh again at the end (line 851).
       Same logical event triggers 2-3 full screen composites.

       Fix: drop the early refreshes; let the trailing one cover them.
       Or set a flag and refresh once.

       DONE: dropped the 5 early vk_screen_refresh calls in the
       close-on-lost-focus branches; every mouse path falls through the
       switch to the single trailing refresh, so a dismiss-click now
       composites once instead of 2-3.  (Verified all switch(zone) cases
       break -- no early continue -- so the close is always painted.)

[x] 5. pt_thread.c refreshes per drain cycle, not per scheduler tick
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

       DONE (approach a): split the two concerns the drain path used to
       fuse.  Each tile still renders its own window immediately
       (vk_window_update, cheap), but instead of compositing the whole
       screen it sets vwm->screen_dirty.  The scheduler grew a generic
       per-step hook (vwm_sched_set_step_cb) -- it stays vdk-agnostic,
       only invoking the callback after each step's dispatch -- and
       vwm.c registers vwm_sched_render, which issues one
       vk_screen_refresh per step when the flag is set, then clears it.
       All ready tiles run inside a single protothread_run() per step,
       so N busy tiles now cost one composite per step instead of N.
       The hook is gated only by screen_dirty (not did_work), so the
       deferred-refresh turn -- which deliberately doesn't report
       did_work -- still composites in the same step.  Single-tile
       behavior is unchanged (same refresh count, same step, just routed
       through the hook); the startup composites and the terminal
       close/EPIPE refresh paths are untouched.  Builds clean; module +
       binary recompiled against the shared struct.  Branch
       vwmterm-coalesce-refresh.  Verified interactively: htop, a caca
       video (continuous high-rate output -- the heaviest coalescing
       case), ps aux, and scrollback (the history-render branch) all
       behaved normally; no stalls, tearing, or missed repaints.

[ ] 6. vk_window_set_title called on no-op transitions
       modules/vwmterm3/events.c lines 300, 322, 403
       Selection enter/exit and OSC title changes call
       vk_window_set_title every time, even when the new title equals
       the current one.  vk_window_set_title strdup's the new title
       and triggers a window redraw.

       Fix: compare against current title (via vk_window_get_title)
       and skip if identical.

[x] 7. Wallpaper cchar_t built per refresh
       bkgd.c  _bkgd_render_small_bricks, _bkgd_render_large_bricks
       RESOLVED by item 1 (now landed): the cchar_t's are built once
       during the wallpaper cache build; the per-refresh path is an
       overwrite() blit of the cached WINDOW, so no per-refresh cchar_t
       construction remains.

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
       accessors so classify_mouse can hit-test them.  Item 2 was done
       via the mechanical approach (a), which still calls these
       accessors through MHIT, so they remain.  Resolved only by
       approach (b) (popups self-register) or the manage_ui_common
       refactor (S2).


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


MEMORY CORRECTNESS (valgrind, 2026-06-17)
-----------------------------------------

From `valgrind --leak-check=full --show-leak-kinds=all ./vwm`
(valgrind 3.22.0), exercising the menus and the manage_* dialogs;
log was at /tmp/vwm-valgrind.log.

These are PRE-EXISTING and unrelated to the recent cleanups -- the
listbox rebuild path (vk_listbox_reset / rebuild_listbox) appears in
none of the records.  For certainty, an A/B run on master should show
the same counts.

  Summary: 184 errors / 33 contexts.
  Leaks: definitely 4,249 B / 20 blocks (14 records);
         indirectly 12,296 B / 151; possibly 27,558 B / 103;
         still reachable 77,181 B / 192.

[ ] M1. ncurses color-tree "Invalid read of size 4" (startup + exit)
        tsearch/tfind/tdelete inside libncursesw, reached via
        _vdk_color_init_extended <- vdk_color_init <- vwm_init at
        startup, and via delscreen <- _vk_screen_dtor at exit.
        ncurses-internal, surfaced by libviper's extended-color setup;
        not directly fixable in vwm.  A libviper/ncurses concern --
        see whether vdk_color_init can avoid it, else suppress.
        (Also noted in libviper/TODO.)

[ ] M2. vwmterm reads an uninitialised value
        "Conditional jump or move depends on uninitialised value(s)"
        at vwmterm_thd (modules/vwmterm3, libvwmterm.so), under
        vwm_sched_trampoline.  A field/var used before it is set in the
        vwmterm thread.  Re-run with --track-origins=yes to pinpoint
        the origin.  Actionable in vwmterm3.

[ ] M3. Leaks at exit (definitely lost: 4,249 B / 20 blocks)
        Top allocation sites are dialog/menu open paths
        (vwm_menu_helper, vwm_dropdown_mouse -> vk_listbox_exec_curr ->
        manage_*_open -> vk_window / vk_scroller / vk_box_create),
        vwmterm allocations, and program load (vwm_programs_load).
        Long-lived objects not freed on exit; the OS reclaims them so
        runtime impact is nil, but a teardown/free pass would zero the
        count.  Lower priority than M2.


SIMPLIFICATION / DEDUP (tiered cleanup review)
----------------------------------------------

A separate code-quality pass (reduce duplication, remove dead code),
run alongside the perf review and classified by tier.  Line counts are
rough estimates from the original analyst pass.

Already merged: the two bugs (module dedup strstr->strcmp; strdupv
off-by-one) and all of Tier 1 -- profile passwd field, panel
strdup_printf("%s")->strdup, signals vwm_sigset heap->stack, panel
freeze/thaw vestige, manage_hotkeys redundant repaints, and
vk_listbox_reset for the 4 full-clear rebuilds.  Remaining:

TIER 1 (leftover)
[x] S1. Dead includes / ghost declarations / stray casts sweep, tree-
        wide (e.g. duplicate includes, vwm_hook_* ghost decls).
        Compile-validated only; verify each before committing.  (The
        (void)signum cast already went with the sigset change.)

        DONE (verifiable subset): removed the vwm_hook_wm_start/stop
        ghost decls in private.h (no definition or caller) and the
        duplicate <signal.h>/<time.h> includes in vwm.c.  The broader
        "unused includes tree-wide" is descoped -- reliable detection
        needs include-what-you-use tooling and the payoff is cosmetic,
        not worth a manual hunt across ~68 files.

THE BIG ONE -- shared manage_ui_common (~450+ lines; spike first)
[x] S2. The three manage_* dialogs each carry private copies of the
        same primitives.  Consolidate into one translation unit:
          warning_popup_show()      x3        ~180
          error_popup_show()        x2-3      ~75
          popup center+clamp        19 sites  ~63
          double-click detection    10 sites  ~55
          popup close lifecycle     ~13 sites ~45
          listbox_scroll_info()     x3        ~22
          one/two-button popup kmio            ~30
        De-risk: extract one helper (e.g. warning_popup), prove it
        across all three dialogs, then move the rest.  Centralizing the
        popup set also subsumes perf items 2 and 13 (classify_mouse
        hit-test boilerplate and the get_X_popup accessor sprawl).
        DONE: listbox_scroll_info + the warning / error / saved /
        confirm / load popup construction now live in manage_ui_common
        (one family per PR).  Left per-dialog by design -- the close
        lifecycle, double-click detection, and one/two-button kmio
        dispatch are each woven around the per-dialog popup pointers the
        dialogs' mouse/keyboard routing depends on.

TIER 2 -- within-file dedup
[ ] S3. manage_settings: popup-lifecycle trios (~90), two-button
        handler (~55), TASK/DATE actions x3 (~45).
[ ] S4. manage_hotkeys: offsetof table for the 13-field
        load/apply/has_changes triplication (~40); scroll twins (~22).
[ ] S5. manage_apps: KEY_UP/DOWN nav dup; dropdown-zone table; dead
        include + dead output params (~40).
[ ] S6. winman: WINDOW_MOVE_* / RESIZE_* siblings -> 2 bodies (~48).
[ ] S7. panel: message-list scan x4 -> 2 finders (~30).
[ ] S8. bkgd: 3 fill-pattern helpers -> one (~20).
[ ] S9. mainmenu: dropdown boilerplate + nav dup.
[ ] S10. modules: find_by_name/title/type share structure -> dedup;
         dead ghost declarations.  (find_by_title is also perf item 11;
         classify_mouse hit-test dedup is perf item 2.)

TIER 3 -- bundled modules
[ ] S11. vwmprint <-> vwmscrshot: 6 shared tool-window helpers
         (center_window, center_pad, make_window, destroy_own_window,
         swap_window, close_session) ~85.
[ ] S12. vwmterm3: scroll logic x4 -> one (~50); 6-block module
         registration -> table (~50); selection-normalization dup (~13).

PARKED (excluded -- would trade simplicity for perf/memory or risk)
  - sched.c n_active counter: adds a field + sync invariant for
    negligible gain at MAX_TASKS=20.
  - modules.c find_by_type first-iteration restructure: low-med risk,
    touches the iteration contract.
