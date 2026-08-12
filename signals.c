#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>

#ifdef VWM_CRASH_BACKTRACE
#include <execinfo.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include "vwm.h"
#include "signals.h"


/*
   Set on every SIGWINCH so the clock poller can tell a reattach happened.

   dtach's `-r winch` sends SIGWINCH to vwm when a client attaches.  ncurses
   only turns SIGWINCH into KEY_RESIZE when the terminal dimensions actually
   changed, so a reattach onto a same-size terminal never runs vwm's resync
   cascade (the freshly attached tty comes up with the mouse off, the cursor
   visible and keypad mode cleared).  The handler records the SIGWINCH here;
   check_destination_resize() in clock.c forces a KEY_RESIZE when this is set
   even though the geometry is unchanged.
*/
volatile sig_atomic_t   vwm_winch_pending = 0;

static struct sigaction vwm_winch_prev;
static int              vwm_winch_prev_valid = 0;

/*
   install handler for signum, preserving the current signal mask and
   leaving sa_flags clear (matches the old calloc-zeroed sigaction).
*/
void
vwm_sigset(int signum, sighandler_t handler)
{
    struct sigaction    action;
    sigset_t            old_mask;

    if(handler == NULL) return;

    memset(&action, 0, sizeof(action));

    // retrieve current signal mask
    sigprocmask(0,NULL,&old_mask);
    action.sa_handler = handler;
    action.sa_mask = old_mask;
    sigaction(signum, &action, NULL);
}

#ifdef VWM_CRASH_BACKTRACE
/*
   Fatal-signal crash dump (cmake -DVWM_CRASH_BACKTRACE=ON).

   On SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL write a glibc backtrace to
       /tmp/vwm-crash-<epoch>-<pid>.log
   then restore SIG_DFL and re-raise so the process still dies (cores
   still work).  Modeled on libvterm's vterm_debug.c:

     - static buffers only (no malloc in the handler)
     - write(2) + backtrace_symbols_fd() only
     - recursion guard
     - prime backtrace() at install time (dlopen is not async-signal-safe)

   localtime/strftime are not async-signal-safe, so the date stamp is
   unix epoch seconds from time(NULL) rather than a broken-down calendar
   date.  stderr is redirected to /dev/null in main(), so this file is
   the only post-mortem record under normal runs.
*/

#define VWM_CRASH_BT_MAX    128

static void                     *crash_bt_buf[VWM_CRASH_BT_MAX];
static volatile sig_atomic_t    crash_closing = 0;

/* async-signal-safe unsigned-to-decimal; returns digit count written */
static int
_vwm_utoa(unsigned long v, char *buf)
{
    char    tmp[32];
    int     i = 0;
    int     j = 0;

    if(v == 0) { buf[0] = '0'; return 1; }
    while(v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; }
    while(i > 0) buf[j++] = tmp[--i];
    return j;
}

/* async-signal-safe unsigned-to-hex (no 0x prefix) */
static int
_vwm_xtoa(unsigned long v, char *buf)
{
    static const char   hex[] = "0123456789abcdef";
    char                tmp[32];
    int                 i = 0;
    int                 j = 0;

    if(v == 0) { buf[0] = '0'; return 1; }
    while(v > 0) { tmp[i++] = hex[v & 0xF]; v >>= 4; }
    while(i > 0) buf[j++] = tmp[--i];
    return j;
}

static const char *
_vwm_signame(int sig)
{
    switch(sig)
    {
        case SIGSEGV:   return "SIGSEGV";
        case SIGABRT:   return "SIGABRT";
        case SIGBUS:    return "SIGBUS";
        case SIGFPE:    return "SIGFPE";
        case SIGILL:    return "SIGILL";
    }
    return "SIG?";
}

static void
_vwm_crash_handler(int sig, siginfo_t *si, void *ctx)
{
    char        path[96];
    char        num[32];
    int         fd;
    int         n;
    int         off;
    const char *name;

    (void)ctx;

    if(crash_closing) _exit(128 + sig);
    crash_closing = 1;

    /* /tmp/vwm-crash-<epoch>-<pid>.log  (epoch = date-ish, pid = unique) */
    memcpy(path, "/tmp/vwm-crash-", 15);
    off = 15;
    off += _vwm_utoa((unsigned long)time(NULL), &path[off]);
    path[off++] = '-';
    off += _vwm_utoa((unsigned long)getpid(), &path[off]);
    memcpy(&path[off], ".log", 5);              /* includes NUL */

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd < 0) fd = STDERR_FILENO;

    write(fd, "vwm crash: ", 11);
    name = _vwm_signame(sig);
    write(fd, name, strlen(name));
    write(fd, " (sig ", 6);
    n = _vwm_utoa((unsigned long)sig, num);
    write(fd, num, n);
    write(fd, ") pid ", 6);
    n = _vwm_utoa((unsigned long)getpid(), num);
    write(fd, num, n);

    if(si != NULL && (sig == SIGSEGV || sig == SIGBUS))
    {
        write(fd, " fault_addr 0x", 14);
        n = _vwm_xtoa((unsigned long)(uintptr_t)si->si_addr, num);
        write(fd, num, n);
    }

    write(fd, " time ", 6);
    n = _vwm_utoa((unsigned long)time(NULL), num);
    write(fd, num, n);
    write(fd, "\n", 1);

    write(fd,
        "(resolve addresses via:  addr2line -e /path/to/vwm <addr>)\n",
        59);
    write(fd, "--- backtrace ---\n", 18);

    n = backtrace(crash_bt_buf, VWM_CRASH_BT_MAX);
    backtrace_symbols_fd(crash_bt_buf, n, fd);

    write(fd, "--- end backtrace ---\n", 22);

    if(fd != STDERR_FILENO) close(fd);

    signal(sig, SIG_DFL);
    raise(sig);
}

void
vwm_crash_handlers_install(void)
{
    struct sigaction    sa;
    void                *dummy[4];

    /* prime libgcc_s so the first backtrace() (which dlopens it) does
       not happen inside the signal handler -- dlopen is not AS-safe */
    backtrace(dummy, 4);

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = _vwm_crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
}

#endif /* VWM_CRASH_BACKTRACE */

void
vwm_SIGIO(int signum)
{
    // noop for now

    (void)signum;
}

void
vwm_SIGWINCH(int signum)
{
    vwm_winch_pending = 1;

    /* chain to ncurses' handler (captured when we installed ours) so its
       own size-change -> KEY_RESIZE bookkeeping keeps working for ordinary
       resizes; we only add the same-size reattach case on top of it. */
    if(vwm_winch_prev_valid
        && (vwm_winch_prev.sa_flags & SA_SIGINFO) == 0
        && vwm_winch_prev.sa_handler != SIG_IGN
        && vwm_winch_prev.sa_handler != SIG_DFL
        && vwm_winch_prev.sa_handler != NULL)
    {
        vwm_winch_prev.sa_handler(signum);
    }

    (void)signum;
}

/*
   Install vwm_SIGWINCH, capturing whatever handler ncurses installed during
   newterm() so we can chain it.  Must be called AFTER curses init -- if it
   runs first, newterm()'s own handler setup overwrites ours.  ncurses only
   claims SIGWINCH when the current disposition is SIG_DFL, so once ours is
   in place it survives later newterm() calls (e.g. teleport).
*/
void
vwm_sigwinch_install(void)
{
    struct sigaction    act;

    memset(&act, 0, sizeof(act));
    act.sa_handler = vwm_SIGWINCH;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;       /* no SA_RESTART: let SIGWINCH break the poll */

    if(sigaction(SIGWINCH, &act, &vwm_winch_prev) == 0)
        vwm_winch_prev_valid = 1;
}

void
vwm_SIGTERM(int signum)
{
    extern int  shutdown;

    (void)signum;

    /* let the main loop unwind so vk_screen_destroy can endwin() the
       active SCREEN -- the terminal would otherwise be left in raw
       mode (especially noticeable after teleport) */
    shutdown = 1;
}


