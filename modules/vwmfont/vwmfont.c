/*
    vwmfont -- large "pixel-art" text from Terminus PSF console fonts.
    See vwmfont-spec.md and vwmfont.h.

    This file is split into: font discovery, gzip+PSF parsing (PSF1 and
    PSF2, including the Unicode table -> glyph-index map), a per-size font
    cache, and the render path that paints the bitmap into a vk_widget_t.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#include <wchar.h>
#include <langinfo.h>

#include <zlib.h>

#include "vwmfont.h"

/* ── size table ───────────────────────────────────────────────── */

/* Per enum value: pixel width/height, weight, the console-setup grid
   suffix (Debian/console-setup naming), and the upstream ter-* token. */
static const struct
{
    int         w;
    int         h;
    char        weight;     /* 'n' normal, 'b' bold, 'v' CRT/VGA-bold */
    const char  *cgrid;     /* e.g. "14", "16", "12x6", "18x10" */
    const char  *token;     /* e.g. "ter-114n" */
}
g_grids[VWMFONT_SIZE_COUNT] =
{
    {  6, 12, 'n', "12x6",  "ter-112n" },
    {  8, 14, 'n', "14",    "ter-114n" },
    {  8, 14, 'b', "14",    "ter-114b" },
    {  8, 14, 'v', "14",    "ter-114v" },
    {  8, 16, 'n', "16",    "ter-116n" },
    {  8, 16, 'b', "16",    "ter-116b" },
    {  8, 16, 'v', "16",    "ter-116v" },
    { 10, 18, 'n', "18x10", "ter-118n" },
    { 10, 18, 'b', "18x10", "ter-118b" },
    { 10, 20, 'n', "20x10", "ter-120n" },
    { 10, 20, 'b', "20x10", "ter-120b" },
    { 11, 22, 'n', "22x11", "ter-122n" },
    { 11, 22, 'b', "22x11", "ter-122b" },
    { 12, 24, 'n', "24x12", "ter-124n" },
    { 12, 24, 'b', "24x12", "ter-124b" },
    { 14, 28, 'n', "28x14", "ter-128n" },
    { 14, 28, 'b', "28x14", "ter-128b" },
    { 16, 32, 'n', "32x16", "ter-132n" },
    { 16, 32, 'b', "32x16", "ter-132b" },
};

/* console-setup glyph-subset prefixes, broadest coverage first */
static const char *g_prefixes[] =
{
    "Uni3", "Uni2", "Lat15", "Lat7", "Lat2", "Lat38",
    "Greek", "CyrSlav", "CyrAsia", "FullCyrAsia"
};
#define NUM_PREFIXES (int)(sizeof(g_prefixes) / sizeof(g_prefixes[0]))

static const char *g_default_dirs[] =
{
    "/usr/share/consolefonts/",     /* Debian / Ubuntu  */
    "/usr/lib/kbd/consolefonts/",   /* Fedora / RHEL    */
    "/usr/share/kbd/consolefonts/", /* Arch             */
};
#define NUM_DEFAULT_DIRS (int)(sizeof(g_default_dirs) / sizeof(g_default_dirs[0]))

/* ── parsed font + cache ──────────────────────────────────────── */

typedef struct
{
    int         width;
    int         height;
    int         charsize;       /* bytes per glyph */
    int         glyph_count;
    uint8_t     *raw;           /* whole decompressed file (owned) */
    uint8_t     *bitmaps;       /* into raw: glyph_count * charsize */
    uint16_t    *cp2glyph;      /* 0x10000 entries; 0xFFFF = none (owned) */
}
vwmfont_font_t;

#define CP_NONE         0xFFFF
#define CP_MAP_SIZE     0x10000
#define MAX_FONT_BYTES  (8 * 1024 * 1024)

static vwmfont_font_t   *g_cache[VWMFONT_SIZE_COUNT];
static char             g_font_dir[1024];      /* from vwmfont_init */

/* ── discovery ────────────────────────────────────────────────── */

static int
file_ok(const char *path)
{
    return access(path, R_OK) == 0;
}

/* console-setup filename for (prefix, size): <prefix>-Terminus[Bold|
   BoldVGA]<cgrid>.psf.gz */
static void
console_name(char *buf, size_t sz, const char *prefix, vwmfont_size_t size)
{
    const char *wp = (g_grids[size].weight == 'b') ? "Bold"
                   : (g_grids[size].weight == 'v') ? "BoldVGA"
                   : "";
    snprintf(buf, sz, "%s-Terminus%s%s.psf.gz",
        prefix, wp, g_grids[size].cgrid);
}

/* Try one directory for the requested size; on success copy the full
   path into out and return 1. */
static int
try_dir(const char *dir, vwmfont_size_t size, char *out, size_t out_sz)
{
    char    path[1024];
    int     p;

    if(dir == NULL || dir[0] == '\0') return 0;

    /* 1. upstream ter-*.psf.gz (some distros / the deploy target) */
    snprintf(path, sizeof(path), "%s/%s.psf.gz", dir, g_grids[size].token);
    if(file_ok(path)) { snprintf(out, out_sz, "%s", path); return 1; }

    /* 2. console-setup <prefix>-Terminus*.psf.gz, broadest first */
    for(p = 0; p < NUM_PREFIXES; p++)
    {
        char name[128];
        console_name(name, sizeof(name), g_prefixes[p], size);
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        if(file_ok(path)) { snprintf(out, out_sz, "%s", path); return 1; }
    }

    return 0;
}

static int
find_font_path(vwmfont_size_t size, char *out, size_t out_sz)
{
    const char  *env;
    int         i;

    if(size < 0 || size >= VWMFONT_SIZE_COUNT) return 0;

    env = getenv("VWMFONT_DIR");
    if(env != NULL && try_dir(env, size, out, out_sz)) return 1;
    if(g_font_dir[0] && try_dir(g_font_dir, size, out, out_sz)) return 1;

    for(i = 0; i < NUM_DEFAULT_DIRS; i++)
        if(try_dir(g_default_dirs[i], size, out, out_sz)) return 1;

    return 0;
}

/* ── gzip read ────────────────────────────────────────────────── */

/* Inflate the whole gzip file into a malloc'd buffer.  Returns 0 on
   success and fills *out / *out_len; -1 on failure. */
static int
gz_read_all(const char *path, uint8_t **out, size_t *out_len)
{
    gzFile      gz;
    uint8_t     *buf = NULL;
    size_t      cap = 0;
    size_t      len = 0;

    gz = gzopen(path, "rb");
    if(gz == NULL) return -1;

    for(;;)
    {
        int     n;

        if(len + 65536 > cap)
        {
            size_t  ncap = cap ? cap * 2 : 131072;
            uint8_t *nb;
            if(ncap > MAX_FONT_BYTES) { gzclose(gz); free(buf); return -1; }
            nb = realloc(buf, ncap);
            if(nb == NULL) { gzclose(gz); free(buf); return -1; }
            buf = nb;
            cap = ncap;
        }

        n = gzread(gz, buf + len, 65536);
        if(n < 0) { gzclose(gz); free(buf); return -1; }
        if(n == 0) break;
        len += (size_t)n;
    }

    gzclose(gz);

    if(len == 0) { free(buf); return -1; }

    *out = buf;
    *out_len = len;
    return 0;
}

/* ── little-endian helpers ────────────────────────────────────── */

static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd_u32(const uint8_t *p)
{ return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

/* Decode one UTF-8 sequence at p (within [p,end)); store codepoint in
   *cp and return bytes consumed, or 0 on malformed/at-end. */
static int
utf8_decode(const uint8_t *p, const uint8_t *end, uint32_t *cp)
{
    if(p >= end) return 0;
    if(p[0] < 0x80) { *cp = p[0]; return 1; }
    if((p[0] & 0xE0) == 0xC0)
    {
        if(p + 1 >= end) return 0;
        *cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        return 2;
    }
    if((p[0] & 0xF0) == 0xE0)
    {
        if(p + 2 >= end) return 0;
        *cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6)
            | (p[2] & 0x3F);
        return 3;
    }
    if((p[0] & 0xF8) == 0xF0)
    {
        if(p + 3 >= end) return 0;
        *cp = ((uint32_t)(p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12)
            | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }
    return 0;
}

static void
map_set(uint16_t *m, uint32_t cp, int glyph)
{
    if(cp < CP_MAP_SIZE && m[cp] == CP_NONE) m[cp] = (uint16_t)glyph;
}

/* ── PSF parsing ──────────────────────────────────────────────── */

static void
font_free(vwmfont_font_t *f)
{
    if(f == NULL) return;
    free(f->raw);
    free(f->cp2glyph);
    free(f);
}

/* PSF1: magic 0x36 0x04.  width 8, charsize = height.  mode bit 0x01 =
   512 glyphs, 0x02 = has Unicode table (UCS-2 LE, 0xFFFF terminates a
   glyph entry, 0xFFFE separates combining sequences). */
static vwmfont_font_t *
parse_psf1(uint8_t *raw, size_t len)
{
    vwmfont_font_t  *f;
    uint8_t         mode;
    int             charsize, count;
    size_t          bm_off, bm_len;

    if(len < 4) return NULL;
    mode = raw[2];
    charsize = raw[3];
    if(charsize <= 0 || charsize > 128) return NULL;

    count = (mode & 0x01) ? 512 : 256;
    bm_off = 4;
    bm_len = (size_t)count * charsize;
    if(bm_off + bm_len > len) return NULL;

    f = calloc(1, sizeof(*f));
    if(f == NULL) return NULL;
    f->cp2glyph = malloc(CP_MAP_SIZE * sizeof(uint16_t));
    if(f->cp2glyph == NULL) { free(f); return NULL; }
    memset(f->cp2glyph, 0xFF, CP_MAP_SIZE * sizeof(uint16_t)); /* CP_NONE */

    f->raw = raw;
    f->width = 8;
    f->charsize = charsize;
    f->height = charsize;          /* PSF1: 1 byte/row, 8 wide */
    f->glyph_count = count;
    f->bitmaps = raw + bm_off;

    if(mode & 0x02)
    {
        const uint8_t *p = raw + bm_off + bm_len;
        const uint8_t *end = raw + len;
        int g = 0;

        while(p + 1 < end && g < count)
        {
            uint16_t v = rd_u16(p);
            p += 2;
            if(v == 0xFFFF) { g++; continue; }
            if(v == 0xFFFE)                 /* skip combining sequence */
            {
                while(p + 1 < end && rd_u16(p) != 0xFFFF) p += 2;
                continue;
            }
            map_set(f->cp2glyph, v, g);
        }
    }
    else
    {
        int c;                              /* no table: identity ASCII */
        for(c = 0; c < count && c < CP_MAP_SIZE; c++)
            f->cp2glyph[c] = (uint16_t)c;
    }

    return f;
}

/* PSF2: magic 0x72 0xB5 0x4A 0x86 then LE u32: version, headersize,
   flags, length, charsize, height, width.  flags bit0 = has Unicode
   table (UTF-8; 0xFF terminates a glyph entry, 0xFE separates combining
   sequences). */
static vwmfont_font_t *
parse_psf2(uint8_t *raw, size_t len)
{
    vwmfont_font_t  *f;
    uint32_t        headersize, flags, count, charsize, height, width;
    int             bpr;
    size_t          bm_off, bm_len;

    if(len < 32) return NULL;
    headersize = rd_u32(raw + 8);
    flags      = rd_u32(raw + 12);
    count      = rd_u32(raw + 16);
    charsize   = rd_u32(raw + 20);
    height     = rd_u32(raw + 24);
    width      = rd_u32(raw + 28);

    if(width == 0 || width > 256 || height == 0 || height > 256) return NULL;
    if(charsize == 0 || charsize > 4096) return NULL;
    if(count == 0 || count > 65536) return NULL;
    if(headersize < 32 || headersize > len) return NULL;

    bpr = (int)((width + 7) / 8);
    if((uint32_t)bpr * height != charsize) return NULL;   /* sanity */

    bm_off = headersize;
    bm_len = (size_t)count * charsize;
    if(bm_off + bm_len > len) return NULL;

    f = calloc(1, sizeof(*f));
    if(f == NULL) return NULL;
    f->cp2glyph = malloc(CP_MAP_SIZE * sizeof(uint16_t));
    if(f->cp2glyph == NULL) { free(f); return NULL; }
    memset(f->cp2glyph, 0xFF, CP_MAP_SIZE * sizeof(uint16_t));

    f->raw = raw;
    f->width = (int)width;
    f->height = (int)height;
    f->charsize = (int)charsize;
    f->glyph_count = (int)count;
    f->bitmaps = raw + bm_off;

    if(flags & 0x01)
    {
        const uint8_t *p = raw + bm_off + bm_len;
        const uint8_t *end = raw + len;
        uint32_t g = 0;

        while(p < end && g < count)
        {
            if(*p == 0xFF) { p++; g++; continue; }
            if(*p == 0xFE)                  /* skip combining sequence */
            {
                p++;
                while(p < end && *p != 0xFF) p++;
                continue;
            }
            {
                uint32_t cp;
                int n = utf8_decode(p, end, &cp);
                if(n <= 0) { p++; continue; }
                map_set(f->cp2glyph, cp, (int)g);
                p += n;
            }
        }
    }
    else
    {
        uint32_t c;
        for(c = 0; c < count && c < CP_MAP_SIZE; c++)
            f->cp2glyph[c] = (uint16_t)c;
    }

    return f;
}

static vwmfont_font_t *
parse_psf(uint8_t *raw, size_t len)
{
    if(len >= 2 && raw[0] == 0x36 && raw[1] == 0x04)
        return parse_psf1(raw, len);
    if(len >= 4 && raw[0] == 0x72 && raw[1] == 0xB5 &&
       raw[2] == 0x4A && raw[3] == 0x86)
        return parse_psf2(raw, len);
    return NULL;
}

/* Load (and cache) the font for a size.  Returns NULL on failure. */
static vwmfont_font_t *
load_font(vwmfont_size_t size)
{
    char            path[1024];
    uint8_t         *raw;
    size_t          len;
    vwmfont_font_t  *f;

    if(size < 0 || size >= VWMFONT_SIZE_COUNT) return NULL;
    if(g_cache[size] != NULL) return g_cache[size];

    if(!find_font_path(size, path, sizeof(path))) return NULL;
    if(gz_read_all(path, &raw, &len) != 0) return NULL;

    f = parse_psf(raw, len);
    if(f == NULL) { free(raw); return NULL; }

    g_cache[size] = f;
    return f;
}

/* pixel (x,y) of glyph index g: 1 = on, 0 = off.  Padding bits beyond
   the glyph width are masked by the x bound. */
static int
glyph_pixel(const vwmfont_font_t *f, int g, int x, int y)
{
    int             bpr;
    const uint8_t   *row;

    if(g < 0 || g >= f->glyph_count) return 0;
    if(x < 0 || x >= f->width || y < 0 || y >= f->height) return 0;

    bpr = (f->width + 7) / 8;
    row = f->bitmaps + (size_t)g * f->charsize + (size_t)y * bpr;
    return (row[x >> 3] >> (7 - (x & 7))) & 1;
}

/* codepoint -> glyph index, with fallback to '?' then glyph 0/blank. */
static int
glyph_for_cp(const vwmfont_font_t *f, uint32_t cp)
{
    if(cp < CP_MAP_SIZE && f->cp2glyph[cp] != CP_NONE)
        return f->cp2glyph[cp];
    if(f->cp2glyph['?'] != CP_NONE) return f->cp2glyph['?'];
    return -1;                      /* render as blank */
}

/* ── public API ───────────────────────────────────────────────── */

int
vwmfont_init(const char *font_dir)
{
    if(font_dir != NULL && font_dir[0] != '\0')
    {
        snprintf(g_font_dir, sizeof(g_font_dir), "%s", font_dir);
    }
    return 0;
}

void
vwmfont_shutdown(void)
{
    int i;
    for(i = 0; i < VWMFONT_SIZE_COUNT; i++)
    {
        font_free(g_cache[i]);
        g_cache[i] = NULL;
    }
    g_font_dir[0] = '\0';
}

const char *
vwmfont_size_token(vwmfont_size_t size)
{
    if(size < 0 || size >= VWMFONT_SIZE_COUNT) return "";
    return g_grids[size].token;
}

/* ── render ───────────────────────────────────────────────────── */

#define MAX_DIM 4096

/* per-widget render metadata, stored in the widget's userptr so colors
   can be (re)applied after render.  Owned; freed by vwmfont_free_widget. */
typedef struct
{
    int         width;          /* widget cell dimensions */
    int         height;
    int         revspace;       /* on-pixels are blanks + A_REVERSE */
    uint8_t     *onoff;         /* width*height: 1 = on pixel */
}
vwmfont_info_t;

static int
utf8_is_locale(void)
{
    const char *cs = nl_langinfo(CODESET);
    return cs != NULL &&
        (strcasecmp(cs, "UTF-8") == 0 || strcasecmp(cs, "UTF8") == 0);
}

static int
cp_is_space(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r';
}

vk_widget_t *
vwmfont_render(const char *utf8_text, vwmfont_size_t size,
    vwmfont_fill_t fill)
{
    vwmfont_font_t  *font;
    vk_widget_t     *widget;
    vwmfont_info_t  *info;
    WINDOW          *canvas;
    const uint8_t   *p, *end;
    int             gw, gh, cols, rows, line, nonblank;
    int             W, H, revspace, li, gi;
    cchar_t         cc_block;
    wchar_t         wblock[2] = { 0x2588, 0 };

    if(utf8_text == NULL) return NULL;
    if(size < 0 || size >= VWMFONT_SIZE_COUNT) return NULL;
    if(fill < 0 || fill >= VWMFONT_FILL_COUNT) fill = VWMFONT_FILL_FULLBLOCK;

    font = load_font(size);
    if(font == NULL) return NULL;
    gw = font->width;
    gh = font->height;
    if(gw <= 0 || gh <= 0) return NULL;

    /* pass 1: measure -- cols = widest line, rows = line count */
    cols = 0; rows = 1; line = 0; nonblank = 0;
    p = (const uint8_t *)utf8_text;
    end = p + strlen(utf8_text);
    while(p < end)
    {
        uint32_t cp;
        int n = utf8_decode(p, end, &cp);
        if(n <= 0) { p++; continue; }
        p += n;
        if(cp == '\n') { rows++; line = 0; continue; }
        line++;
        if(line > cols) cols = line;
        if(!cp_is_space(cp)) nonblank++;
    }
    if(cols == 0 || nonblank == 0) return NULL;     /* empty / whitespace */

    W = cols * gw;
    H = rows * gh;
    if(W <= 0 || H <= 0 || W > MAX_DIM || H > MAX_DIM) return NULL;

    widget = vk_widget_create(W, H);
    if(widget == NULL) return NULL;
    canvas = vk_widget_get_canvas(widget);
    if(canvas == NULL) { vk_widget_destroy(widget); return NULL; }

    info = calloc(1, sizeof(*info));
    if(info == NULL) { vk_widget_destroy(widget); return NULL; }
    info->onoff = calloc((size_t)W * H, 1);
    if(info->onoff == NULL)
    { free(info); vk_widget_destroy(widget); return NULL; }
    info->width = W;
    info->height = H;

    revspace = (fill == VWMFONT_FILL_FULLBLOCK) && !utf8_is_locale();
    info->revspace = revspace;
    setcchar(&cc_block, wblock, A_NORMAL, 0, NULL);  /* used only for block */

    werase(canvas);

    /* pass 2: paint each glyph's on-pixels; off-pixels stay blank */
    li = 0; gi = 0;
    p = (const uint8_t *)utf8_text;
    while(p < end)
    {
        uint32_t cp;
        int g, gx, gy, n;

        n = utf8_decode(p, end, &cp);
        if(n <= 0) { p++; continue; }
        p += n;
        if(cp == '\n') { li++; gi = 0; continue; }

        g = glyph_for_cp(font, cp);
        for(gy = 0; gy < gh; gy++)
        {
            int cy = li * gh + gy;
            for(gx = 0; gx < gw; gx++)
            {
                int cx = gi * gw + gx;
                int on = (g >= 0) && glyph_pixel(font, g, gx, gy);

                info->onoff[(size_t)cy * W + cx] = (uint8_t)on;
                if(!on) continue;

                if(fill == VWMFONT_FILL_FULLBLOCK && !revspace)
                    mvwadd_wch(canvas, cy, cx, &cc_block);
                else if(fill == VWMFONT_FILL_O)
                    mvwaddch(canvas, cy, cx, 'O');
                else if(fill == VWMFONT_FILL_X)
                    mvwaddch(canvas, cy, cx, 'X');
                /* revspace on-pixels stay blank; the fill is supplied by
                   A_REVERSE in vwmfont_apply_colors */
            }
        }
        gi++;
    }

    vk_widget_set_userptr(widget, info);
    return widget;
}

void
vwmfont_apply_colors(vk_widget_t *widget, short fg, short bg)
{
    vwmfont_info_t  *info;
    WINDOW          *canvas;
    short           pair;
    int             x, y;

    if(widget == NULL) return;
    info = (vwmfont_info_t *)vk_widget_get_userptr(widget);
    canvas = vk_widget_get_canvas(widget);
    if(info == NULL || canvas == NULL) return;

    vk_widget_set_colors(widget, fg, bg);
    pair = vdk_color_pair(fg, bg);      /* mvwchgat takes a full short pair */

    for(y = 0; y < info->height; y++)
        for(x = 0; x < info->width; x++)
        {
            int on = info->onoff[(size_t)y * info->width + x];
            attr_t a = (on && info->revspace) ? A_REVERSE : A_NORMAL;
            mvwchgat(canvas, y, x, 1, a, pair, NULL);
        }
}

void
vwmfont_free_widget(vk_widget_t *widget)
{
    vwmfont_info_t  *info;

    if(widget == NULL) return;
    info = (vwmfont_info_t *)vk_widget_get_userptr(widget);
    if(info != NULL)
    {
        free(info->onoff);
        free(info);
        vk_widget_set_userptr(widget, NULL);
    }
    vk_widget_destroy(widget);
}
