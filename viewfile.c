/*************************************************************************
 * All portions of code are copyright by their respective author/s.
 * Copyright (C) 2007-2026 Bryan Christ <bryan.christ@gmail.com>
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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <vdk.h>

#include "viewfile.h"

/*
    The file is a header followed by rows * cols cells in row order:

        vwm_viewfile_header_t               32 bytes
        vdk_cell_t  [rows * cols]           16 bytes each

    It is written in place with ordinary writes and read with ordinary
    reads -- nothing is mapped into memory.  The header's `seq` is how
    a reader gets a whole picture rather than half of two: the writer
    makes it odd before it changes any row and even again afterwards,
    so a reader that sees the same even number before and after its
    read has read a consistent screen, and one that does not reads
    again.

    The cells are libviper's (vdk_cells_read_row does the decoding);
    everything about the file is here.  It is in the byte order and
    layout of the machine it runs on: writer and reader are on the same
    host by design.
*/

#define VWM_VIEWFILE_MAGIC       0x564D5756      /* the bytes "VWMV" */
#define VWM_VIEWFILE_VERSION     1

/* the largest canvas a reader will believe: guards against a file that
   is not one of ours, or is damaged */
#define VWM_VIEWFILE_MAX_SIDE    4096

/* how many times a reader tries for a consistent read before it gives
   up until the next poll */
#define VWM_VIEWFILE_READ_TRIES  5

typedef struct
{
    uint32_t    magic;          /* VWM_VIEWFILE_MAGIC                          */
    uint32_t    version;        /* VWM_VIEWFILE_VERSION                        */
    uint32_t    cols;           /* width of the canvas, in cells          */
    uint32_t    rows;           /* height of the canvas, in cells         */
    uint32_t    seq;            /* change counter; odd while rows are
                                   being written                          */
    uint32_t    reserved[3];    /* zero                                   */
}
vwm_viewfile_header_t;

struct _vwm_viewfile_writer_s
{
    int                 fd;
    char                *path;
    vwm_viewfile_header_t    header;
    vdk_cell_t          *shadow;    /* what the file holds: rows * cols   */
    vdk_cell_t          *scratch;   /* one row being decoded: cols        */
    unsigned char       *dirty;     /* per row: differs from the file     */
};

struct _vwm_viewfile_reader_s
{
    int                 fd;
    uint32_t            cols;
    uint32_t            rows;
    uint32_t            seq;        /* of the contents held in `cells`    */
    int                 loaded;     /* `cells` holds a screen             */
    vdk_cell_t          *cells;     /* rows * cols                        */
    size_t              cells_sz;   /* cells `cells` has room for         */
    vdk_cell_t          *incoming;  /* a read in progress                 */
    size_t              incoming_sz;    /* cells `incoming` has room for  */
};

/* write all of a buffer at an offset; 0, or -1 if it could not be */
static int
viewfile_write_at(int fd, const void *buf, size_t len, off_t offset)
{
    const char  *p = (const char *)buf;
    ssize_t     n;

    while(len > 0)
    {
        n = pwrite(fd, p, len, offset);

        if(n < 0)
        {
            if(errno == EINTR) continue;
            return -1;
        }

        p += n;
        len -= (size_t)n;
        offset += n;
    }

    return 0;
}

/* read exactly `len` bytes at an offset; 0, or -1 if they are not all
   there (a file still being set up, or one that was cut short) */
static int
viewfile_read_at(int fd, void *buf, size_t len, off_t offset)
{
    char    *p = (char *)buf;
    ssize_t n;

    while(len > 0)
    {
        n = pread(fd, p, len, offset);

        if(n < 0)
        {
            if(errno == EINTR) continue;
            return -1;
        }

        if(n == 0) return -1;

        p += n;
        len -= (size_t)n;
        offset += n;
    }

    return 0;
}

/* where row `y` starts in a file `cols` cells wide */
static off_t
viewfile_row_offset(uint32_t cols, uint32_t y)
{
    return (off_t)sizeof(vwm_viewfile_header_t)
        + (off_t)y * (off_t)cols * (off_t)sizeof(vdk_cell_t);
}

vwm_viewfile_writer_t*
vwm_viewfile_writer_create(const char *path)
{
    vwm_viewfile_writer_t *writer;

    if(path == NULL || path[0] == '\0') return NULL;

    writer = (vwm_viewfile_writer_t *)calloc(1, sizeof(vwm_viewfile_writer_t));
    if(writer == NULL) return NULL;

    writer->path = strdup(path);
    if(writer->path == NULL)
    {
        free(writer);
        return NULL;
    }

    /* always a file of our own making: whatever was at this path --
       a stale file from a run that crashed, or something planted there
       -- is removed, and the new one must not already exist or be a
       link.  only the owner can read it: it is a copy of the screen */
    unlink(path);
    writer->fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
        0600);

    if(writer->fd < 0)
    {
        free(writer->path);
        free(writer);
        return NULL;
    }

    writer->header.magic = VWM_VIEWFILE_MAGIC;
    writer->header.version = VWM_VIEWFILE_VERSION;

    /* a header with no size yet: a reader that opens the file before
       the first update finds a valid, empty canvas */
    if(viewfile_write_at(writer->fd, &writer->header, sizeof(writer->header), 0) != 0)
    {
        vwm_viewfile_writer_destroy(writer);
        return NULL;
    }

    return writer;
}

/*
    The canvas has a different size from the one in the file (or this
    is the first update).  Make room for the new size and mark every
    row as needing to be written: the shadow is filled with a value no
    decoded row can equal.
*/
static int
viewfile_resize(vwm_viewfile_writer_t *writer, uint32_t cols, uint32_t rows)
{
    vdk_cell_t      *shadow;
    vdk_cell_t      *scratch;
    unsigned char   *dirty;
    size_t          count = (size_t)cols * rows;

    shadow = (vdk_cell_t *)malloc(count * sizeof(vdk_cell_t));
    scratch = (vdk_cell_t *)malloc((size_t)cols * sizeof(vdk_cell_t));
    dirty = (unsigned char *)calloc(rows, 1);

    if(shadow == NULL || scratch == NULL || dirty == NULL)
    {
        free(shadow);
        free(scratch);
        free(dirty);
        return -1;
    }

    /* all bits set is not a cell the decode can produce */
    memset(shadow, 0xFF, count * sizeof(vdk_cell_t));

    free(writer->shadow);
    free(writer->scratch);
    free(writer->dirty);
    writer->shadow = shadow;
    writer->scratch = scratch;
    writer->dirty = dirty;

    writer->header.cols = cols;
    writer->header.rows = rows;

    return 0;
}

int
vwm_viewfile_writer_update(vwm_viewfile_writer_t *writer, WINDOW *win)
{
    vdk_cell_t  *held;
    int         rows, cols;
    int         y;
    int         changed = 0;
    int         written = 0;
    int         resized = 0;
    size_t      row_sz;

    if(writer == NULL || win == NULL) return -1;

    getmaxyx(win, rows, cols);
    if(rows <= 0 || cols <= 0) return -1;
    if(rows > VWM_VIEWFILE_MAX_SIDE || cols > VWM_VIEWFILE_MAX_SIDE) return -1;

    if(writer->shadow == NULL
        || writer->header.cols != (uint32_t)cols
        || writer->header.rows != (uint32_t)rows)
    {
        if(viewfile_resize(writer, (uint32_t)cols, (uint32_t)rows) != 0)
            return -1;

        resized = 1;
    }

    row_sz = (size_t)cols * sizeof(vdk_cell_t);

    /*
        first, with the file untouched, work out which rows differ from
        what it holds.  decoding the canvas is the slow part; doing it
        before anything is written keeps the time during which readers
        must wait down to the writes themselves.
    */
    for(y = 0; y < rows; y++)
    {
        held = writer->shadow + (size_t)y * cols;
        writer->dirty[y] = 0;

        if(vdk_cells_read_row(win, y, writer->scratch, cols) < 0) continue;

        /* the file already holds this row as it is now */
        if(memcmp(held, writer->scratch, row_sz) == 0) continue;

        memcpy(held, writer->scratch, row_sz);
        writer->dirty[y] = 1;
        changed++;
    }

    if(changed == 0) return 0;

    /* tell readers a change is under way before any of it is in the
       file.  a new size goes out in this same header, so no reader
       pairs old rows with new numbers */
    writer->header.seq++;              /* now odd */

    if(viewfile_write_at(writer->fd, &writer->header, sizeof(writer->header), 0) != 0)
        return -1;

    /* the file is exactly as long as the canvas needs */
    if(resized
        && ftruncate(writer->fd, viewfile_row_offset((uint32_t)cols,
            (uint32_t)rows)) != 0)
        return -1;

    for(y = 0; y < rows; y++)
    {
        if(!writer->dirty[y]) continue;

        if(viewfile_write_at(writer->fd, writer->shadow + (size_t)y * cols, row_sz,
            viewfile_row_offset((uint32_t)cols, (uint32_t)y)) != 0)
            return -1;

        written++;
    }

    /* the change is complete: readers may trust what they read again */
    writer->header.seq++;              /* even */

    if(viewfile_write_at(writer->fd, &writer->header, sizeof(writer->header), 0) != 0)
        return -1;

    return written;
}

void
vwm_viewfile_writer_destroy(vwm_viewfile_writer_t *writer)
{
    if(writer == NULL) return;

    /* gone from the directory: a reader that still has it open keeps
       reading the last screen until it looks for the file again */
    if(writer->path != NULL) unlink(writer->path);
    if(writer->fd >= 0) close(writer->fd);

    free(writer->path);
    free(writer->shadow);
    free(writer->scratch);
    free(writer->dirty);
    free(writer);
}

vwm_viewfile_reader_t*
vwm_viewfile_reader_create(const char *path)
{
    vwm_viewfile_reader_t    *reader;

    if(path == NULL || path[0] == '\0') return NULL;

    reader = (vwm_viewfile_reader_t *)calloc(1, sizeof(vwm_viewfile_reader_t));
    if(reader == NULL) return NULL;

    reader->fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);

    if(reader->fd < 0)
    {
        free(reader);
        return NULL;
    }

    return reader;
}

int
vwm_viewfile_reader_poll(vwm_viewfile_reader_t *reader)
{
    vwm_viewfile_header_t    before;
    vwm_viewfile_header_t    after;
    vdk_cell_t          *swap;
    struct stat         st;
    size_t              count;
    size_t              swap_sz;
    int                 tries;

    if(reader == NULL) return -1;

    /* the writer removes the file when it stops: no links left
       means there will be nothing more to read */
    if(fstat(reader->fd, &st) != 0 || st.st_nlink == 0) return -1;

    for(tries = 0; tries < VWM_VIEWFILE_READ_TRIES; tries++)
    {
        if(viewfile_read_at(reader->fd, &before, sizeof(before), 0) != 0)
            return -1;

        if(before.magic != VWM_VIEWFILE_MAGIC || before.version != VWM_VIEWFILE_VERSION)
            return -1;

        if(before.cols > VWM_VIEWFILE_MAX_SIDE || before.rows > VWM_VIEWFILE_MAX_SIDE)
            return -1;

        /* nothing published since the screen we hold */
        if(reader->loaded && before.seq == reader->seq) return 0;

        /* a change is being written right now: look again shortly */
        if(before.seq & 1)
        {
            usleep(200);
            continue;
        }

        count = (size_t)before.cols * before.rows;

        /* a canvas with no size yet: valid, and empty */
        if(count == 0)
        {
            reader->cols = 0;
            reader->rows = 0;
            reader->seq = before.seq;
            reader->loaded = 1;
            return 1;
        }

        if(count > reader->incoming_sz)
        {
            swap = (vdk_cell_t *)realloc(reader->incoming,
                count * sizeof(vdk_cell_t));
            if(swap == NULL) return -1;

            reader->incoming = swap;
            reader->incoming_sz = count;
        }

        /* rows not all there yet (the file is still being lengthened):
           the same as a change under way */
        if(viewfile_read_at(reader->fd, reader->incoming,
            count * sizeof(vdk_cell_t), sizeof(vwm_viewfile_header_t)) != 0)
        {
            usleep(200);
            continue;
        }

        if(viewfile_read_at(reader->fd, &after, sizeof(after), 0) != 0)
            return -1;

        /* the counter moved while we read: some rows are old and some
           new.  throw the read away and take another */
        if(after.seq != before.seq) continue;

        /* a whole screen: it becomes the one we hold.  the buffers
           change places so the old one is reused for the next read */
        swap = reader->cells;
        swap_sz = reader->cells_sz;
        reader->cells = reader->incoming;
        reader->cells_sz = reader->incoming_sz;
        reader->incoming = swap;
        reader->incoming_sz = swap_sz;

        reader->cols = before.cols;
        reader->rows = before.rows;
        reader->seq = before.seq;
        reader->loaded = 1;

        return 1;
    }

    /* the writer was busy every time: keep what we have */
    return 0;
}

int
vwm_viewfile_reader_get_size(vwm_viewfile_reader_t *reader, int *cols, int *rows)
{
    if(reader == NULL || !reader->loaded) return -1;

    if(cols != NULL) *cols = (int)reader->cols;
    if(rows != NULL) *rows = (int)reader->rows;

    return 0;
}

const vdk_cell_t*
vwm_viewfile_reader_get_row(vwm_viewfile_reader_t *reader, int y)
{
    if(reader == NULL || !reader->loaded || reader->cells == NULL)
        return NULL;

    if(y < 0 || (uint32_t)y >= reader->rows) return NULL;

    return reader->cells + (size_t)y * reader->cols;
}

void
vwm_viewfile_reader_destroy(vwm_viewfile_reader_t *reader)
{
    if(reader == NULL) return;

    if(reader->fd >= 0) close(reader->fd);

    free(reader->cells);
    free(reader->incoming);
    free(reader);
}
