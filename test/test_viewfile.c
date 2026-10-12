/*
    test_viewfile -- headless checks of the view file's writer and
    reader (viewfile.c): a screen written to a file and read back.

    Exit status 0 when every check passes.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <vdk.h>

#include "../viewfile.h"

static int  passed = 0;
static int  failed = 0;

/* record one check */
static void
check(const char *what, int ok)
{
    if(ok) passed++;
    else failed++;

    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
}

/* does the reader hold exactly what `win` shows? */
static int
same_as_window(vwm_viewfile_reader_t *reader, WINDOW *win)
{
    vdk_cell_t          row[512];
    const vdk_cell_t    *held;
    int                 rows, cols;
    int                 r_cols = 0, r_rows = 0;
    int                 y;

    getmaxyx(win, rows, cols);

    if(vwm_viewfile_reader_get_size(reader, &r_cols, &r_rows) != 0) return 0;
    if(r_cols != cols || r_rows != rows) return 0;

    for(y = 0; y < rows; y++)
    {
        held = vwm_viewfile_reader_get_row(reader, y);
        if(held == NULL) return 0;

        vdk_cells_read_row(win, y, row, cols);
        if(memcmp(held, row, (size_t)cols * sizeof(vdk_cell_t)) != 0)
            return 0;
    }

    return 1;
}

/* milliseconds since some fixed moment */
static double
now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

int
main(void)
{
    vk_screen_t         *screen;
    vwm_viewfile_writer_t     *writer;
    vwm_viewfile_reader_t        *reader;
    vwm_viewfile_reader_t        *second;
    WINDOW              *win;
    WINDOW              *bigger;
    struct stat         st;
    char                path[128];
    int                 cols = 0, rows = 0;
    int                 n, i, x, y;
    double              t0, t1;

    setenv("TERM", "xterm-256color", 1);
    setlocale(LC_ALL, "");

    snprintf(path, sizeof(path), "/tmp/vwm_test_viewfile.%d", (int)getpid());

    screen = vk_screen_create_detached(80, 24);
    if(screen == NULL)
    {
        printf("FAIL  could not create a detached screen\n");
        return 1;
    }

    vdk_color_init();

    win = newwin(10, 40, 0, 0);
    wattr_set(win, A_BOLD, vdk_color_pair(COLOR_YELLOW, COLOR_BLUE), NULL);
    mvwaddstr(win, 0, 0, "first row");
    wattr_set(win, A_NORMAL, vdk_color_pair(COLOR_WHITE, COLOR_RED), NULL);
    mvwaddstr(win, 5, 3, "sixth row");

    check("nothing to read before a writer exists",
        vwm_viewfile_reader_create(path) == NULL);

    writer = vwm_viewfile_writer_create(path);
    check("a writer is created", writer != NULL);
    if(writer == NULL) return 1;

    check("the file is readable by its owner alone",
        stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);

    /* a reader that arrives before the first update */
    reader = vwm_viewfile_reader_create(path);
    check("a reader opens the file", reader != NULL);
    check("before the first update there is an empty canvas",
        vwm_viewfile_reader_poll(reader) == 1
        && vwm_viewfile_reader_get_size(reader, &cols, &rows) == 0
        && cols == 0 && rows == 0);

    n = vwm_viewfile_writer_update(writer, win);
    check("the first update writes every row", n == 10);

    check("the reader sees something new", vwm_viewfile_reader_poll(reader) == 1);
    check("what it read is what the window shows",
        same_as_window(reader, win));
    check("a second poll finds nothing new", vwm_viewfile_reader_poll(reader) == 0);

    check("an update with nothing changed writes nothing",
        vwm_viewfile_writer_update(writer, win) == 0);
    check("and the reader is not disturbed", vwm_viewfile_reader_poll(reader) == 0);

    /* one row changes */
    mvwaddstr(win, 5, 3, "SIXTH ROW");
    check("one changed row is one row written",
        vwm_viewfile_writer_update(writer, win) == 1);
    check("the reader picks the change up",
        vwm_viewfile_reader_poll(reader) == 1 && same_as_window(reader, win)
        && vwm_viewfile_reader_get_row(reader, 5)[3].ch == 'S');

    /* a reader that joins late gets the whole screen at once */
    second = vwm_viewfile_reader_create(path);
    check("a late reader gets the current screen",
        second != NULL && vwm_viewfile_reader_poll(second) == 1
        && same_as_window(second, win));

    /* the canvas changes size */
    bigger = newwin(20, 100, 0, 0);
    wattr_set(bigger, A_NORMAL, vdk_color_pair(COLOR_GREEN, COLOR_BLACK), NULL);
    for(i = 0; i < 20; i++) mvwprintw(bigger, i, i, "row %d", i);

    check("a bigger canvas is written whole",
        vwm_viewfile_writer_update(writer, bigger) == 20);
    check("both readers follow the new size",
        vwm_viewfile_reader_poll(reader) == 1 && same_as_window(reader, bigger)
        && vwm_viewfile_reader_poll(second) == 1 && same_as_window(second, bigger));
    check("the file is exactly header plus cells",
        stat(path, &st) == 0
        && st.st_size == (off_t)(32 + 20 * 100 * sizeof(vdk_cell_t)));

    check("back to the smaller canvas",
        vwm_viewfile_writer_update(writer, win) == 10
        && vwm_viewfile_reader_poll(reader) == 1 && same_as_window(reader, win));
    check("and the file shrank with it",
        stat(path, &st) == 0
        && st.st_size == (off_t)(32 + 10 * 40 * sizeof(vdk_cell_t)));

    check("a row outside the screen held is refused",
        vwm_viewfile_reader_get_row(reader, 10) == NULL
        && vwm_viewfile_reader_get_row(reader, -1) == NULL);
    check("NULL arguments are refused",
        vwm_viewfile_writer_update(NULL, win) == -1
        && vwm_viewfile_writer_update(writer, NULL) == -1
        && vwm_viewfile_reader_poll(NULL) == -1
        && vwm_viewfile_writer_create(NULL) == NULL
        && vwm_viewfile_reader_create(NULL) == NULL);

    /* what it costs: a screen the size of a large terminal, first
       unchanged (the common case) and then with every row changed */
    delwin(bigger);
    bigger = newwin(80, 300, 0, 0);
    for(y = 0; y < 80; y++)
        for(x = 0; x < 300; x++)
            mvwaddch(bigger, y, x, (chtype)('a' + (x + y) % 26)
                | COLOR_PAIR(vdk_color_pair((x + y) % 8, y % 8)));

    vwm_viewfile_writer_update(writer, bigger);

    t0 = now_ms();
    for(i = 0; i < 20; i++) vwm_viewfile_writer_update(writer, bigger);
    t1 = now_ms();
    printf("INFO  300x80 unchanged: %.2f ms per update\n", (t1 - t0) / 20);

    t0 = now_ms();
    for(i = 0; i < 20; i++)
    {
        for(y = 0; y < 80; y++) mvwaddch(bigger, y, i, (chtype)('0' + i % 10));
        vwm_viewfile_writer_update(writer, bigger);
    }
    t1 = now_ms();
    printf("INFO  300x80 every row changed: %.2f ms per update\n",
        (t1 - t0) / 20);

    t0 = now_ms();
    for(i = 0; i < 20; i++)
    {
        mvwaddch(bigger, 0, 0, (chtype)('A' + i));
        vwm_viewfile_writer_update(writer, bigger);
        vwm_viewfile_reader_poll(reader);
    }
    t1 = now_ms();
    printf("INFO  300x80 one row changed, plus a full read: %.2f ms\n",
        (t1 - t0) / 20);
    check("the reader holds the large screen", same_as_window(reader, bigger));

    /*
        a reader in another process while the writer is busy.  every
        update gives all 80 rows the same letter in column 0, written
        one row at a time, so a read that caught half of an update
        would hold two different letters.  none may.
    */
    {
        pid_t   child;
        int     status = 0;

        /* start from a screen that already has one letter down
           column 0, so the first thing the reader sees qualifies */
        for(y = 0; y < 80; y++) mvwaddch(bigger, y, 0, (chtype)'z');
        vwm_viewfile_writer_update(writer, bigger);

        fflush(stdout);
        child = fork();

        if(child == 0)
        {
            vwm_viewfile_reader_t        *r = vwm_viewfile_reader_create(path);
            const vdk_cell_t    *cells;
            double              until = now_ms() + 600;
            int                 screens = 0;
            int                 torn = 0;
            int                 yy;

            if(r == NULL) _exit(3);

            while(now_ms() < until)
            {
                if(vwm_viewfile_reader_poll(r) != 1) continue;

                screens++;
                cells = vwm_viewfile_reader_get_row(r, 0);
                for(yy = 1; yy < 80; yy++)
                {
                    if(vwm_viewfile_reader_get_row(r, yy)[0].ch != cells[0].ch)
                        torn++;
                }
            }

            /* 0: saw plenty of screens and none was torn */
            _exit((torn > 0) ? 1 : ((screens < 10) ? 2 : 0));
        }

        t0 = now_ms();
        for(i = 0; now_ms() - t0 < 700; i++)
        {
            for(y = 0; y < 80; y++)
                mvwaddch(bigger, y, 0, (chtype)('A' + i % 26));
            vwm_viewfile_writer_update(writer, bigger);
        }

        waitpid(child, &status, 0);
        check("a reader in another process never sees half an update",
            WIFEXITED(status) && WEXITSTATUS(status) == 0);
        if(WIFEXITED(status) && WEXITSTATUS(status) != 0)
            printf("INFO  reader exit code %d (1 torn, 2 too few screens, 3 no file)\n",
                WEXITSTATUS(status));
    }

    /* the writer stops */
    vwm_viewfile_writer_destroy(writer);
    check("the file is removed when publishing stops", stat(path, &st) != 0);
    check("a reader learns that it stopped", vwm_viewfile_reader_poll(reader) == -1);
    check("and still has the last screen",
        vwm_viewfile_reader_get_row(reader, 0) != NULL);

    vwm_viewfile_reader_destroy(reader);
    vwm_viewfile_reader_destroy(second);
    delwin(win);
    delwin(bigger);
    vk_screen_destroy(screen);

    printf("%d of %d passed\n", passed, passed + failed);

    return (failed == 0) ? 0 : 1;
}
