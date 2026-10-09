/*************************************************************************
 * All portions of code are copyright by their respective author/s.
 * Copyright (C) 2007      Bryan Christ <bryan.christ@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *----------------------------------------------------------------------*/

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <locale.h>
#include <inttypes.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>

#ifdef __linux
#include <sys/klog.h>
#endif

#include <vdk.h>
#include <vkmio.h>
#include "protothread.h"
#include "sched.h"

#include "vwm.h"
#include "private.h"
#include "bkgd.h"
#include "mainmenu.h"
#include "panel.h"
#include "modules.h"
#include "settings.h"
#include "signals.h"
#include "winman.h"
#include "list.h"
#include "clock.h"
#include "poll_input_thd.h"
#include "programs.h"
#include "ctl.h"
#include "attach.h"

static void
vwm_cursor_overlay(vk_screen_t *screen, int surface_id, WINDOW *canvas);

static int
vwm_on_surface_change(vk_object_t *object, int event, void *anything);

static int
vwm_on_teleport(vk_object_t *object, int event, void *anything);

static void
vwm_launch(const char *ctl_path);

static void
vwm_launch_report(const char *msg);

static void
vwm_sched_render(void *arg);

vwm_sched_t             *sched = NULL;
int                     shutdown = 0;

/* the size the session's screen starts at when it starts on no
   terminal (see vwm_launch); 0 x 0 when it starts on one, under dtach */
static int              vwm_start_w = 0;
static int              vwm_start_h = 0;

/* in the background session while it starts: where to tell the command
   that launched it how that went (see vwm_launch_report).  -1 otherwise. */
static int              vwm_report_fd = -1;

// store argv and argc for use elsewhere (with modules)
char    **vwm_argv;
int     vwm_argc;

int main(int argc,char **argv)
{
    vwm_t                   *vwm = NULL;
    extern char             **vwm_argv;
    extern int              vwm_argc;
	int		      		    fd;
	char		      		*locale = NULL;

    extern int              shutdown;
    extern vwm_sched_t      *sched;

    vwm_sched_ctx_t         *ctx_clock;
    vwm_sched_ctx_t         *ctx_poll_input;
    MEVENT                  mouse_event;

    /* answer probes before ncurses or the control socket exist.  a
       piped `vwm --version` must not walk on to vwm_ctl_init() and
       become a second window manager -- that process would unlink the
       live session's control socket. */
    {
        int i;

        for(i = 1; i < argc; i++)
        {
            if(strcmp(argv[i], "--version") == 0 ||
               strcmp(argv[i], "-V") == 0)
            {
                printf("vwm %s\n", VWM_VERSION);
                return 0;
            }
            if(strcmp(argv[i], "--help") == 0 ||
               strcmp(argv[i], "-h") == 0)
            {
                printf(
                    "Usage: vwm [options]\n"
                    "Start a session in the background and show it on "
                    "this terminal.\n"
                    "vwm-resume brings a running session to a terminal; "
                    "vwm-stop ends it.\n\n"
                    "  -h, --help             show this help and exit\n"
                    "  -V, --version          show version and exit\n"
                    "      --ignore-tty-size  skip the 80x25 minimum "
                    "check\n");
                return 0;
            }
        }
    }

    /* everything that can be refused is refused here, on the terminal,
       before anything is started */
    {
        bool            ignore_tty_size = false;
        struct winsize  ws;
        int             tries;
        int             i;

        for(i = 1; i < argc; i++)
        {
            if(strcmp(argv[i], "--ignore-tty-size") == 0)
            {
                ignore_tty_size = true;
                break;
            }
        }

        /* under dtach the pty has no size (0x0) until the client
           attaches and reports one, and the client clears the screen
           first -- slow on a framebuffer console.  Wait up to 2s for a
           size rather than fail on a reading that only means "not
           known yet". */
        memset(&ws, 0, sizeof(ws));
        for(tries = 0; tries < 200; tries++)
        {
            if(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) != 0) break;
            if(ws.ws_col != 0 || ws.ws_row != 0) break;
            if(getenv("VWM_SOCK") == NULL) break;
            usleep(10000);
        }

        if(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0
            && (ws.ws_col != 0 || ws.ws_row != 0))
        {
            if(!ignore_tty_size && (ws.ws_col < 80 || ws.ws_row < 25))
            {
                fprintf(stderr,
                    "vwm: terminal too small (%dx%d). "
                    "Minimum size is 80x25.\n"
                    "Use --ignore-tty-size to bypass "
                    "this check.\n",
                    ws.ws_col, ws.ws_row);
                return 1;
            }

            /* the session's screen starts out the size of this terminal */
            vwm_start_w = ws.ws_col;
            vwm_start_h = ws.ws_row;
        }
    }

    /* refuse to start if another vwm is already serving the control
       socket.  a second full session would unlink the live one's path
       (ctl_init) and, worse, delete it again on exit -- leaving the
       original listener bound to an unnamed inode and vwm-msg dead for
       the rest of the session.  do this before ncurses so the message
       is visible and the terminal is untouched. */
    {
        char    ctl_path[4096];

        if(vwm_ctl_preflight(ctl_path, sizeof(ctl_path)) != 0)
        {
            fprintf(stderr,
                "vwm: a session is already running (%s).\n"
                "     vwm-resume brings it to this terminal; "
                "vwm-stop ends it.\n",
                ctl_path);
            return 1;
        }

        /* the session runs in the background, on no terminal; this
           command starts it and then attaches this terminal to it.
           Only the background session returns from here.  (Under
           dtach -- vwm-start sets VWM_SOCK -- vwm still runs on the
           terminal dtach gives it.) */
        if(getenv("VWM_SOCK") == NULL)
            vwm_launch(ctl_path);
        else
        {
            vwm_start_w = 0;
            vwm_start_h = 0;
        }
    }

    sched = vwm_sched_init();

    // setup clock task (NORMAL priority)
    ctx_clock = calloc(1, sizeof(vwm_sched_ctx_t));
    ctx_clock->shutdown = &shutdown;

    // setup input-polling task (HIGH priority)
    ctx_poll_input = calloc(1, sizeof(vwm_sched_ctx_t));
    ctx_poll_input->anything = &mouse_event;
    ctx_poll_input->shutdown = &shutdown;

    vwm_sched_task_create(sched, ctx_clock,
                vwm_clock_driver, VWM_SCHED_NORMAL);
    vwm_sched_task_create(sched, ctx_poll_input,
                vwm_poll_input, VWM_SCHED_HIGH);

    vwm_argc = argc;
    vwm_argv = argv;

	/*
        set the locale to the default settings (as configured by env).
		this is required for ncurses to work properly.
    */
    locale = getenv("LANG");
    if(locale == NULL) locale = "en_US.UTF-8";

    setlocale(LC_ALL, locale);

	// print some debug information
	printf("%s\n\r", locale);
	printf("ncurses = %d.%d (%d)\n\r", NCURSES_VERSION_MAJOR,
		NCURSES_VERSION_MINOR,NCURSES_VERSION_PATCH);
	fflush(NULL);

#ifdef __linux
    // suppress printk messages.  klogctl() is linux specific.
	klogctl(6, NULL, 0);
    printf("VWM running on Linux\n\r");
#endif

    // supress STDERR
	fd = open("/dev/null", O_WRONLY);
	if(fd == -1) exit(0);
	dup2(fd, STDERR_FILENO);

	// ignore terminal interrupt signal
    vwm_sigset(SIGINT, SIG_IGN);

    // unwind cleanly on SIGTERM so the terminal gets restored
    vwm_sigset(SIGTERM, vwm_SIGTERM);
    vwm_sigset(SIGPIPE, SIG_IGN);

#ifdef _DEBUG
    vwm_sigset(SIGILL, vwm_backtrace);
    vwm_sigset(SIGSEGV, vwm_backtrace);
    vwm_sigset(SIGFPE, vwm_backtrace);
#endif

    /* nothing asks for SIGIO any more -- the scheduler waits on the
       keyboard, the mouse and every pty by descriptor -- but its
       default action is to kill the process, so make sure a stray one
       cannot. */
    vwm_sigset(SIGIO, SIG_IGN);

	// use the integrated window manager
	vwm = vwm_init();

    /* no screen, no session */
    if(vwm->screen == NULL)
    {
        vwm_launch_report("could not set up the screen (is TERM usable?)");
        return 1;
    }

    /* now that newterm() has run (inside vwm_init), claim SIGWINCH so a
       same-size dtach reattach still drives the resync cascade -- chains
       ncurses' own handler, so ordinary resizes are unaffected. */
    vwm_sigwinch_install();

    vwm_panel_init(vwm);

    vk_screen_refresh(vwm->screen);

    vwm_modules_preload(vwm);
    vwm_menubar_init();
    vwm_settings_load(vwm);
    vwm_apply_surface_count(vwm->surface_count);
    /* settings + surfaces are now both live; seed each desktop's bkgd */
    vwm_apply_desktop_bkgd_all();
    /* the first vk_screen_refresh above cached the active desktop's
       wallpaper using the built-in defaults, before settings_load
       supplied the saved pattern/color.  Drop that stale cache so the
       refresh below rebuilds it from the loaded settings -- otherwise
       the real wallpaper does not appear until the first resize. */
    vwm_invalidate_wallpaper_cache_all();
    vwm_programs_load(vwm);

    vk_screen_refresh(vwm->screen);

    /* control socket: inherit VWM_CONTROL_SOCK into every child */
    /* a connection on it ends the scheduler's sleep; the step hook
       below (vwm_ctl_poll) then serves it.  no task owns it, hence the
       NULL. */
    if(vwm_ctl_init() == 0)
        vwm_sched_wake_fd_add(sched, vwm_ctl_listen_fd(), NULL);
    else if(vwm_report_fd >= 0)
    {
        /* a background session nobody can reach is no use to anyone */
        vwm_launch_report("could not create the control socket");
        vk_screen_destroy(vwm->screen);
        return 1;
    }

    /* up and listening: the command that started us can attach now */
    vwm_launch_report("ok");

    /* coalesce vterm composites: drain tasks mark the screen dirty and
       this hook issues one refresh per scheduler step (see item 5). */
    vwm_sched_set_step_cb(sched, vwm_sched_render, vwm);

    vwm_sched_run(sched, &shutdown);

    vk_kmio_shutdown(vk_screen_get_fd(vwm->screen));
    vk_screen_destroy(vwm->screen);

    /* the terminal is back in order: let the client waiting on it go.
       Before the scheduler is torn down, which is where the client's
       connection is registered. */
    vwm_ctl_release_client("stopped");

    vwm_ctl_shutdown();
    vwm_sched_deinit(sched);
    fsync(fd);
	close(fd);

	return 0;
}

/*
    Start the session in the background and attach this terminal to it.

    Called once at startup with nothing started yet.  It returns only in
    the new background process, which goes on to become the session.
    The command the user ran never returns from here: it waits to hear
    how the start went, then (when it is on a terminal) attaches that
    terminal and stays as its foreground job until the session leaves --
    see attach.h.

    The session is a process of its own, in a session of its own, with
    no controlling terminal: closing the terminal it was started from,
    or logging out, does not touch it.
*/
static void
vwm_launch(const char *ctl_path)
{
    const char  *tty = NULL;
    const char  *term = getenv("TERM");
    char        tty_buf[64];
    char        line[256];
    size_t      used = 0;
    int         report[2];
    int         devnull;
    pid_t       pid;
    int         i;

    /* the terminal this command is on, if any */
    for(i = 0; i < 3 && tty == NULL; i++)
        if(isatty(i)) tty = ttyname(i);

    if(tty != NULL)
    {
        snprintf(tty_buf, sizeof(tty_buf), "%s", tty);
        tty = tty_buf;
    }

    /* started from no terminal (a script, a service): a default size */
    if(vwm_start_w < 1 || vwm_start_h < 1)
    {
        vwm_start_w = 80;
        vwm_start_h = 25;
    }

    if(pipe(report) != 0)
    {
        fprintf(stderr, "vwm: pipe: %s\n", strerror(errno));
        exit(1);
    }

    pid = fork();
    if(pid < 0)
    {
        fprintf(stderr, "vwm: fork: %s\n", strerror(errno));
        exit(1);
    }

    if(pid == 0)
    {
        /* --- the session-to-be --- */
        close(report[0]);

        /* leave the launching terminal's session, then fork once more
           so that this process is not a session leader: a session
           leader that opens a terminal can end up owning it, and the
           session opens whichever terminal it is attached to */
        setsid();
        if(fork() != 0) _exit(0);

        /* no handle on the launching terminal may survive here */
        devnull = open("/dev/null", O_RDWR);
        if(devnull >= 0)
        {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if(devnull > STDERR_FILENO) close(devnull);
        }

        /* the programs it runs must not inherit the report pipe */
        fcntl(report[1], F_SETFD, FD_CLOEXEC);
        vwm_report_fd = report[1];

        /* nothing hangs up on a process with no terminal, but a stray
           SIGHUP should not end a session meant to outlive them */
        vwm_sigset(SIGHUP, SIG_IGN);

        return;
    }

    /* --- the command the user ran --- */
    close(report[1]);

    /* the middle process exits at once; collect it */
    waitpid(pid, NULL, 0);

    /* one line from the session: "ok", or what went wrong.  End of file
       with no line means it died before it could say. */
    while(used < sizeof(line) - 1)
    {
        ssize_t n = read(report[0], line + used, sizeof(line) - 1 - used);

        if(n < 0 && errno == EINTR) continue;
        if(n <= 0) break;

        used += (size_t)n;
        if(memchr(line, '\n', used) != NULL) break;
    }
    line[used] = '\0';
    line[strcspn(line, "\n")] = '\0';
    close(report[0]);

    if(strcmp(line, "ok") != 0)
    {
        fprintf(stderr, "vwm: the session did not start%s%s\n",
            (line[0] != '\0') ? ": " : ".", line);
        exit(1);
    }

    /* not on a terminal: the session is up, and that is all there is
       to do from here */
    if(tty == NULL)
    {
        printf("vwm: session started.  vwm-resume brings it to a "
            "terminal; vwm-stop ends it.\n");
        exit(0);
    }

    exit(vwm_attach_run(ctl_path, tty, term));
}

/*
    In the background session: tell the command that launched it how the
    start went -- "ok", or the reason it failed -- and close the line.
    Does nothing when nobody is listening (under dtach, or once it has
    been said).
*/
static void
vwm_launch_report(const char *msg)
{
    if(vwm_report_fd < 0) return;

    if(write(vwm_report_fd, msg, strlen(msg)) < 0
        || write(vwm_report_fd, "\n", 1) < 0)
    {
        /* the launching command is gone; carry on regardless */
    }

    close(vwm_report_fd);
    vwm_report_fd = -1;
}

/*
    scheduler per-step render hook.  the vterm drain tasks update their
    own windows and set vwm->screen_dirty rather than each compositing
    the whole screen; this fires once per step and collapses N busy
    tiles into a single vk_screen_refresh.  cheap no-op when nothing
    drained this step.
*/
static void
vwm_sched_render(void *arg)
{
    vwm_t   *vwm = (vwm_t *)arg;

    vwm_ctl_poll();

    if(vwm->screen_dirty)
    {
        vk_screen_refresh(vwm->screen);
        vwm->screen_dirty = 0;
    }
}

vwm_t*
vwm_init(void)
{
	static vwm_t    *vwm = NULL;

	if(vwm == NULL)
	{
 		vwm = (vwm_t*)calloc(1, sizeof(vwm_t));

        /* the session starts on no terminal, at the size of the one it
           was launched from, and is attached afterwards.  Under dtach
           (no start size) it starts on the terminal dtach gives it. */
        if(vwm_start_w > 0 && vwm_start_h > 0)
            vwm->screen = vk_screen_create_detached(vwm_start_w, vwm_start_h);
        else
            vwm->screen = vk_screen_create();

        /* main() reports this and gives up */
        if(vwm->screen == NULL) return vwm;

        vdk_color_init();
        vwm_input_rearm(vwm);

        vk_screen_set_wallpaper(vwm->screen, vwm_bkgd_simple_normal);

        strncpy(vwm->task_indicator_action, "none", NAME_MAX - 1);
        strncpy(vwm->date_click_action, "calendar", NAME_MAX - 1);
        vwm->screensaver_cmd[0] = '\0';
        vwm->screensaver_timeout = 0;
        vwm->clipboard_mode = VWM_CLIPBOARD_BOTH;
        vwm->show_hostname = 0;             /* off by default */
        vwm->hostname_fg = COLOR_WHITE;
        vwm->hostname_bg = COLOR_BLUE;
        vwm->hostname_font = -1;            /* Basic (plain one-row label) */
        vwm->hostname_fill = 0;             /* full block */
        {
            /* sensible defaults per desktop -- diverse colors so a
               fresh vwm still has the original per-surface identity */
            short defaults[VWM_MAX_DESKTOPS] = {
                COLOR_BLUE, COLOR_RED, COLOR_CYAN,
                COLOR_GREEN, COLOR_MAGENTA, COLOR_YELLOW
            };
            int   k;
            for(k = 0; k < VWM_MAX_DESKTOPS; k++)
            {
                vwm->desktop_color[k] = defaults[k];
                vwm->desktop_fg[k] = COLOR_BLACK;
                vwm->desktop_wallpaper[k] = VWM_WALLPAPER_STIPLE;
            }
        }
        vwm->surface_count = 3;

        vwm->decks = calloc(vwm->surface_count, sizeof(vk_deck_t *));

        vwm->decks[0] = vk_deck_create();
        vk_deck_set_shadow(vwm->decks[0], true);
        vk_screen_attach_widget(vwm->screen, 0, VK_WIDGET(vwm->decks[0]));
        vk_object_register_event(VK_OBJECT(vwm->decks[0]),
            VK_EVENT_ON_FINALIZE, vwm_on_deck_finalize, NULL);

        vk_screen_add_surface(vwm->screen);
        vwm->decks[1] = vk_deck_create();
        vk_deck_set_shadow(vwm->decks[1], true);
        vk_screen_attach_widget(vwm->screen, 1, VK_WIDGET(vwm->decks[1]));
        vk_object_register_event(VK_OBJECT(vwm->decks[1]),
            VK_EVENT_ON_FINALIZE, vwm_on_deck_finalize, NULL);

        vk_screen_add_surface(vwm->screen);
        vwm->decks[2] = vk_deck_create();
        vk_deck_set_shadow(vwm->decks[2], true);
        vk_screen_attach_widget(vwm->screen, 2, VK_WIDGET(vwm->decks[2]));
        vk_object_register_event(VK_OBJECT(vwm->decks[2]),
            VK_EVENT_ON_FINALIZE, vwm_on_deck_finalize, NULL);

        vwm->deck = vwm->decks[0];

        vk_object_register_event(VK_OBJECT(vwm->screen),
            VK_EVENT_ON_SURFACE_CHANGE, vwm_on_surface_change, NULL);

        vk_object_register_event(VK_OBJECT(vwm->screen),
            VK_EVENT_ON_TELEPORT, vwm_on_teleport, NULL);

        INIT_LIST_HEAD(&vwm->module_list);

        vwm->hotkey_menu = VWM_HOTKEY_MENU;
        vwm->hotkey_wm = VWM_HOTKEY_WM;
        vwm->hotkey_close = 17;
        vwm->hotkey_cycle = KEY_TAB;
        vwm->hotkey_move_up = KEY_UP;
        vwm->hotkey_move_down = KEY_DOWN;
        vwm->hotkey_move_left = KEY_LEFT;
        vwm->hotkey_move_right = KEY_RIGHT;
        vwm->hotkey_grow_h = '+';
        vwm->hotkey_shrink_h = '-';
        vwm->hotkey_grow_w = '>';
        vwm->hotkey_shrink_w = '<';
        vwm->hotkey_desktop = (27 | (100 << 8));
        vwm->hotkey_detach = 28;        /* Ctrl-\, the key dtach uses */
        {
            /* the console pointer is drawn by vwm.  A session that
               starts on no terminal gets it when it is attached to a
               console (vwm_adopt_apply). */
            const char *term = getenv("TERM");
            if(term != NULL && strcmp(term, "linux") == 0
                && !vk_screen_is_detached(vwm->screen))
            {
                vwm->show_cursor = true;
                vk_screen_set_overlay(vwm->screen, vwm_cursor_overlay);
            }
        }

        // load user profile
        vwm_profile_init(vwm);
    }

	return vwm;
}

void
vwm_input_rearm(vwm_t *vwm)
{
    if(vwm == NULL) return;

    /* re-emit the mouse enable escapes and restore non-blocking input
       against the current tty.  kmio writes the escapes straight to the
       fd, so they have to be resent whenever that fd's terminal may have
       changed: at startup, after teleport (a new fd), and on a dtach
       reattach (a new outer terminal, possibly after `reset`).  On an
       ordinary resize it is a harmless no-op. */
    vk_kmio_init(vk_screen_get_fd(vwm->screen), VWM_KMIO_FLAGS);
    nodelay(stdscr, TRUE);
}

/*
    the console number of /dev/ttyN (1-63), or 0 for any other terminal.
*/
static int
vwm_tty_vc(const char *tty)
{
    char    *end;
    long    vc;

    if(tty == NULL || strncmp(tty, "/dev/tty", 8) != 0) return 0;
    if(tty[8] < '0' || tty[8] > '9') return 0;

    vc = strtol(tty + 8, &end, 10);
    if(*end != '\0' || vc < 1 || vc > 63) return 0;

    return (int)vc;
}

static struct
{
    bool    pending;
    bool    has_term;
    char    term[64];
    int     vc;
}
vwm_adopt_held;

/*
    Do the adopt.  pty == NULL rebuilds the screen where it is.  The
    environment is the hand-off to libviper: TERM picks the terminfo
    entry and the UTF-8 or ASCII glyphs, VK_GPM_VC names the console
    for the GPM mouse when the screen is not on it directly (dtach).
    Returns 0, or -1 if the screen could not be rebuilt (the old
    terminal type is restored).
*/
static int
vwm_adopt_apply(const char *pty, const char *term, int vc)
{
    vwm_t       *vwm = vwm_get_instance();
    const char  *cur = getenv("TERM");
    char        old_term[64];
    char        buf[16];
    bool        had_term = (cur != NULL);
    bool        term_changed;
    bool        console;
    int         retval = 0;

    const char  *old_vc_env = getenv("VK_GPM_VC");
    char        old_vc[16];
    bool        had_vc = (old_vc_env != NULL);

    snprintf(old_vc, sizeof(old_vc), "%s", had_vc ? old_vc_env : "");
    snprintf(old_term, sizeof(old_term), "%s", had_term ? cur : "");
    term_changed = (term != NULL && term[0] != '\0'
        && strcmp(old_term, term) != 0);

    if(term_changed) setenv("TERM", term, 1);

    if(vc > 0)
    {
        snprintf(buf, sizeof(buf), "%d", vc);
        setenv("VK_GPM_VC", buf, 1);
    }
    else
        unsetenv("VK_GPM_VC");

    /* forget the old GPM verdict; the next fetch re-reads both */
    vk_kmio_gpm_reset();

    if(pty != NULL || term_changed)
    {
        /* emits VK_EVENT_ON_TELEPORT -> vwm_on_teleport re-arms input */
        if(vk_screen_adopt(vwm->screen, pty, NULL) != 0)
        {
            /* still where we were: put the description back */
            if(term_changed)
            {
                if(had_term) setenv("TERM", old_term, 1);
                else unsetenv("TERM");
            }
            if(had_vc) setenv("VK_GPM_VC", old_vc, 1);
            else unsetenv("VK_GPM_VC");
            vk_kmio_gpm_reset();
            retval = -1;
        }
    }
    else
        vwm_input_rearm(vwm);

    /* gpm draws no pointer for a client; on the console vwm draws its
       own.  Follow whichever terminal type we ended up on. */
    cur = getenv("TERM");
    console = (cur != NULL && strcmp(cur, "linux") == 0);
    vwm->show_cursor = console;
    vk_screen_set_overlay(vwm->screen, console ? vwm_cursor_overlay : NULL);

    vwm->screen_dirty = 1;

    return retval;
}

/* see vwm.h.  The entry point for the control socket's attach and adopt. */
int
vwm_adopt_terminal(const char *tty, const char *term, const char **err)
{
    vwm_t       *vwm = vwm_get_instance();
    const char  *here;
    const char  *dummy;
    int         vc;

    if(err == NULL) err = &dummy;
    *err = NULL;

    if(vwm == NULL || tty == NULL)
    {
        *err = "no vwm";
        return -1;
    }

    vc = vwm_tty_vc(tty);
    here = ttyname(vk_screen_get_fd(vwm->screen));

    /* no type given: a Linux console is the one terminal whose type
       the path gives away; anything else keeps the current type */
    if((term == NULL || term[0] == '\0') && vc > 0) term = "linux";

    if(getenv("VWM_SOCK") != NULL)
    {
        /* dtach: nothing to move, and nobody attached to show it to yet */
        vwm_adopt_held.pending = true;
        vwm_adopt_held.vc = vc;
        vwm_adopt_held.has_term = (term != NULL && term[0] != '\0');
        snprintf(vwm_adopt_held.term, sizeof(vwm_adopt_held.term), "%s",
            vwm_adopt_held.has_term ? term : "");
        return 0;
    }

    /* already there: rebuild in place rather than open it again */
    if(here != NULL && strcmp(here, tty) == 0) tty = NULL;

    if(vwm_adopt_apply(tty, term, vc) != 0)
    {
        *err = "adopt failed";
        return -1;
    }

    return 0;
}

/* see vwm.h */
bool
vwm_is_headless(void)
{
    vwm_t   *vwm = vwm_get_instance();

    return vwm != NULL && vk_screen_is_detached(vwm->screen);
}

/* see vwm.h */
void
vwm_go_headless(void)
{
    vwm_t   *vwm = vwm_get_instance();

    if(vwm == NULL || vk_screen_is_detached(vwm->screen)) return;

    /* no terminal, so no console and no console mouse: drop the GPM
       connection and forget which console it was for */
    unsetenv("VK_GPM_VC");
    vk_kmio_gpm_reset();

    /* libviper moves the screen off the terminal, keeping its size and
       contents, and emits VK_EVENT_ON_TELEPORT -- vwm_on_teleport then
       re-arms input against the new (null) screen exactly as it does
       after any move */
    if(vk_screen_detach(vwm->screen) != 0) return;

    /* the console pointer is drawn by vwm; nobody is looking */
    vwm->show_cursor = false;
    vk_screen_set_overlay(vwm->screen, NULL);

    vwm->screen_dirty = 1;

    /* the terminal is back the way it was found.  The client that was
       holding it for us can go now, and its shell takes over. */
    vwm_ctl_release_client("detached");
}

/*
    Why the session cannot be detached from where it is, or NULL when it
    can.  One case is left: under dtach the dtach client owns the
    terminal, and its own detach key does the job.
*/
static const char *
vwm_detach_blocker(void)
{
    if(getenv("VWM_SOCK") != NULL)
        return "Running under dtach: detach with its own key";

    return NULL;
}

/* see vwm.h */
bool
vwm_can_detach(void)
{
    return vwm_detach_blocker() == NULL;
}

/* see vwm.h */
int
vwm_detach(const char **why)
{
    const char  *blocker = vwm_detach_blocker();

    if(why != NULL) *why = blocker;
    if(blocker != NULL) return -1;

    /* leaving the terminal hands it back: libviper restores its modes,
       and the client waiting there is let go (vwm_go_headless) */
    vwm_go_headless();

    return 0;
}

/* see vwm.h */
void
vwm_stop(void)
{
    extern int  shutdown;
    vwm_t       *vwm = vwm_get_instance();
    int         i;
    int         j;

    if(vwm == NULL) return;

    /* hang up every terminal's program first, all at once, so they
       exit side by side while the scheduler winds down.  Each window's
       own close then finds its program already gone instead of waiting
       for it in turn. */
    for(i = 0; i < vwm->surface_count; i++)
    {
        if(vwm->decks[i] == NULL) continue;

        for(j = 0; j < vk_deck_count(vwm->decks[i]); j++)
        {
            vk_widget_t *w = vk_deck_get_widget(vwm->decks[i], j);

            if(w != NULL) vk_object_emit(VK_OBJECT(w), VWM_EVENT_ON_HANGUP);
        }
    }

    /* every task sees this on its next turn (the scheduler wakes them
       all while it is set) and returns */
    shutdown = 1;
}

/* see vwm.h.  Called on KEY_RESIZE: a dtach client has just attached. */
bool
vwm_adopt_apply_pending(void)
{
    if(!vwm_adopt_held.pending) return false;

    vwm_adopt_held.pending = false;
    vwm_adopt_apply(NULL,
        vwm_adopt_held.has_term ? vwm_adopt_held.term : NULL,
        vwm_adopt_held.vc);

    return true;
}

void
vwm_apply_surface_count(int new_count)
{
    vwm_t   *vwm = vwm_get_instance();
    int     old_count = vwm->surface_count;
    int     i;

    if(new_count < 2) new_count = 2;
    if(new_count > 6) new_count = 6;
    if(new_count == old_count) return;

    if(new_count > old_count)
    {
        vwm->decks = realloc(vwm->decks, new_count * sizeof(vk_deck_t *));

        for(i = old_count; i < new_count; i++)
        {
            vk_screen_add_surface(vwm->screen);
            vwm->decks[i] = vk_deck_create();
            vk_deck_set_shadow(vwm->decks[i], true);
            vk_screen_attach_widget(vwm->screen, i,
                VK_WIDGET(vwm->decks[i]));
            vk_object_register_event(VK_OBJECT(vwm->decks[i]),
                VK_EVENT_ON_FINALIZE, vwm_on_deck_finalize, NULL);
        }

        vwm->surface_count = new_count;

        /* push bkgd onto the freshly-created surfaces */
        for(i = old_count; i < new_count; i++)
            vwm_apply_desktop_bkgd(i);

        return;
    }

    int target = new_count - 1;

    for(i = old_count - 1; i >= new_count; i--)
    {
        while(vk_deck_get_widget(vwm->decks[i], 0) != NULL)
        {
            vk_widget_t *w = vk_deck_get_widget(vwm->decks[i], 0);
            vk_deck_remove_widget(vwm->decks[i], w);
            vk_deck_add_widget(vwm->decks[target], w, VK_DECK_BOTTOM);
        }

        vk_screen_detach_widget(vwm->screen, i,
            VK_WIDGET(vwm->decks[i]));
        vk_deck_destroy(vwm->decks[i]);
        vk_screen_del_surface(vwm->screen, i);

        /* release the cached wallpaper for the surface we just dropped */
        vwm_invalidate_wallpaper_cache(i);
    }

    vwm->decks = realloc(vwm->decks, new_count * sizeof(vk_deck_t *));
    vwm->surface_count = new_count;

    if(vk_screen_get_active_surface(vwm->screen) >= new_count)
        vk_screen_set_surface(vwm->screen, target);

    vwm->deck = vwm->decks[vk_screen_get_active_surface(vwm->screen)];
}

static int
vwm_on_surface_change(vk_object_t *object, int event, void *anything)
{
    vwm_t       *vwm;
    VWM_PANEL   *panel;
    int         old_surface;
    int         new_surface;

    (void)object;
    (void)event;
    (void)anything;

    vwm = vwm_get_instance();
    panel = vwm_panel_get_data();

    new_surface = vk_screen_get_active_surface(vwm->screen);

    for(old_surface = 0; old_surface < vwm->surface_count; old_surface++)
    {
        if(old_surface == new_surface) continue;

        vk_screen_detach_widget(vwm->screen, old_surface,
            VK_WIDGET(panel->box));
        vk_screen_detach_widget(vwm->screen, old_surface,
            VK_WIDGET(panel->status_box));
    }

    vk_screen_attach_widget(vwm->screen, new_surface,
        VK_WIDGET(panel->box));
    vk_screen_attach_widget(vwm->screen, new_surface,
        VK_WIDGET(panel->status_box));

    vwm->deck = vwm->decks[new_surface];

    /*
        re-decorate every window on the now-active deck so exactly its
        top shows focused.  windows can carry a stale focus border after
        being moved between desktops (Manage Desktop) without a redraw,
        which otherwise leaves two windows looking focused on the
        destination desktop.
    */
    {
        int i, n = vk_deck_count(vwm->deck);
        for(i = 0; i < n; i++)
        {
            vk_widget_t *w = vk_deck_get_widget(vwm->deck, i);
            if(w != NULL) vk_window_update(VK_WINDOW(w));
        }
    }

    /* the "(N) Windows" count is per-desktop -- recount for the new deck */
    vwm_window_menu_refresh();

    return 0;
}

static void
vwm_cursor_overlay(vk_screen_t *screen, int surface_id, WINDOW *canvas)
{
    vwm_t   *vwm = vwm_get_instance();
    int     colors;

    (void)screen;
    (void)surface_id;

    if(!vwm->show_cursor) return;

    colors = COLOR_PAIR(vdk_color_pair(COLOR_YELLOW, COLOR_YELLOW));
    mvwaddch(canvas, vwm->cursor_y, vwm->cursor_x, ' ' | colors);
}

/*
    teleport gives us a brand-new ncurses SCREEN -- color pairs, mouse
    mask, and the non-blocking flag we set at startup all live in the
    old SCREEN.  Re-arm them on the new one, then trigger the existing
    resize cascade so the panel / status bar / open dialogs match the
    new geometry.
*/
static int
vwm_on_teleport(vk_object_t *object, int event, void *anything)
{
    vwm_t   *vwm;

    (void)object;
    (void)event;
    (void)anything;

    vwm = vwm_get_instance();
    if(vwm == NULL) return 0;

    vdk_color_init();

    /* the wallpaper-cache WINDOWs belong to the OLD SCREEN that's about
       to be torn down.  delwin across a different SCREEN corrupts
       ncurses state (same reason libviper leaks the surface canvases on
       teleport), so null the slots without freeing -- next refresh in
       the new SCREEN lazily allocates fresh caches. */
    vwm_invalidate_wallpaper_cache_all_orphan();

    /* re-arm everything kmio set up at startup against the new SCREEN:
       mousemask + mouseinterval are SCREEN-local ncurses state, and
       the \033[?1003h hover escape has to land on the new fd (kmio
       writes it directly to whatever fd we hand it) */
    vwm_input_rearm(vwm);

    /* the new terminal may be of another type: re-pick the panel's
       UTF-8 / ASCII glyphs.  Everything else that asks (wallpaper,
       window buttons, menus) asks at draw time and follows by itself. */
    vwm_panel_refresh_glyphs();

    /* queue a KEY_RESIZE so the input task runs the same cascade it
       does for a real terminal resize (panel + status bar + dialogs),
       and wake it: the key is inside ncurses, not on a descriptor */
    ungetch(KEY_RESIZE);
    vwm_input_wake();

    return 0;
}
