#include <stdlib.h>
#include <string.h>

#include <ncursesw/curses.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "screenshot_priv.h"

/* ── color palette ──────────────────────────────────────────────────────
    ncurses color indices are mapped to RGB through a fixed internal table
    modelled on the standard xterm 256-color palette.  this keeps output
    deterministic regardless of terminal quirks.  Colors redefined at run
    time with init_color() (e.g. truecolor maps) are not reflected here;
    the base 8/16 ANSI colors that vwm actually uses are covered exactly.
*/

typedef struct { uint8_t r, g, b; } rgb8_t;

static rgb8_t   s_palette[256];
static int      s_palette_ready = 0;

static const rgb8_t s_ansi16[16] =
{
    {   0,   0,   0 }, { 205,   0,   0 }, {   0, 205,   0 }, { 205, 205,   0 },
    {   0,   0, 238 }, { 205,   0, 205 }, {   0, 205, 205 }, { 229, 229, 229 },
    { 127, 127, 127 }, { 255,   0,   0 }, {   0, 255,   0 }, { 255, 255,   0 },
    {  92,  92, 255 }, { 255,   0, 255 }, {   0, 255, 255 }, { 255, 255, 255 }
};

static void
palette_init(void)
{
    static const int cube[6] = { 0, 95, 135, 175, 215, 255 };
    int i, r, g, b;

    if(s_palette_ready) return;

    for(i = 0; i < 16; i++)
        s_palette[i] = s_ansi16[i];

    i = 16;
    for(r = 0; r < 6; r++)
        for(g = 0; g < 6; g++)
            for(b = 0; b < 6; b++)
            {
                s_palette[i].r = (uint8_t)cube[r];
                s_palette[i].g = (uint8_t)cube[g];
                s_palette[i].b = (uint8_t)cube[b];
                i++;
            }

    for(i = 0; i < 24; i++)
    {
        int v = 8 + i * 10;
        s_palette[232 + i].r = (uint8_t)v;
        s_palette[232 + i].g = (uint8_t)v;
        s_palette[232 + i].b = (uint8_t)v;
    }

    s_palette_ready = 1;
}

static rgb8_t
color_to_rgb(short idx, rgb8_t dflt)
{
    if(idx < 0 || idx >= 256) return dflt;
    return s_palette[idx];
}

/* ── pixel helpers ─────────────────────────────────────────────────────── */

static void
fill_rect(uint8_t *img, int W, int H, int x0, int y0, int w, int h, rgb8_t c)
{
    int x, y;

    for(y = y0; y < y0 + h; y++)
    {
        uint8_t *row;

        if(y < 0 || y >= H) continue;
        row = img + ((size_t)y * W + x0) * 3;

        for(x = x0; x < x0 + w; x++, row += 3)
        {
            if(x < 0 || x >= W) continue;
            row[0] = c.r;
            row[1] = c.g;
            row[2] = c.b;
        }
    }
}

static void
draw_hline(uint8_t *img, int W, int H, int x0, int y, int w, rgb8_t c)
{
    int x;

    if(y < 0 || y >= H) return;

    for(x = x0; x < x0 + w; x++)
    {
        uint8_t *p;

        if(x < 0 || x >= W) continue;
        p = img + ((size_t)y * W + x) * 3;
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
    }
}

/* alpha-blend an 8-bit gray FreeType bitmap of color fg over the image */
static void
blit_glyph(uint8_t *img, int W, int H, const FT_Bitmap *bmp,
    int x0, int y0, rgb8_t fg)
{
    unsigned int gx, gy;

    if(bmp->pixel_mode != FT_PIXEL_MODE_GRAY) return;
    if(bmp->pitch <= 0) return;

    for(gy = 0; gy < bmp->rows; gy++)
    {
        const uint8_t   *src = bmp->buffer + (size_t)gy * bmp->pitch;
        int             py = y0 + (int)gy;
        uint8_t         *dst;

        if(py < 0 || py >= H) continue;

        for(gx = 0; gx < bmp->width; gx++)
        {
            int     px = x0 + (int)gx;
            uint8_t a = src[gx];

            if(a == 0) continue;
            if(px < 0 || px >= W) continue;

            dst = img + ((size_t)py * W + px) * 3;
            dst[0] = (uint8_t)((fg.r * a + dst[0] * (255 - a)) / 255);
            dst[1] = (uint8_t)((fg.g * a + dst[1] * (255 - a)) / 255);
            dst[2] = (uint8_t)((fg.b * a + dst[2] * (255 - a)) / 255);
        }
    }
}

/* ── renderer ──────────────────────────────────────────────────────────── */

int
vwmscrshot_render(const scrshot_grid_t *grid, const scrshot_cfg_t *cfg,
    uint8_t **out_rgb, int *out_w, int *out_h)
{
    FT_Library  lib   = NULL;
    FT_Face     face  = NULL;
    FT_Face     bold  = NULL;
    uint8_t     *img  = NULL;
    int         ascender, descender;
    int         cell_w, cell_h;
    int         img_w, img_h;
    int         row, col;
    int         rc = VWM_SHOT_OK;

    if(grid == NULL || cfg == NULL || out_rgb == NULL) return VWM_SHOT_ERR_ALLOC;
    if(grid->cells == NULL || grid->rows <= 0 || grid->cols <= 0)
        return VWM_SHOT_ERR_ALLOC;

    palette_init();

    if(FT_Init_FreeType(&lib) != 0) { rc = VWM_SHOT_ERR_FONT; goto done; }

    if(FT_New_Face(lib, cfg->font_path, 0, &face) != 0)
    {
        rc = VWM_SHOT_ERR_FONT;
        goto done;
    }

    if(FT_Set_Pixel_Sizes(face, 0, cfg->font_px) != 0)
    {
        rc = VWM_SHOT_ERR_FONT;
        goto done;
    }

    /* optional bold face; absence simply falls back to double-strike */
    if(cfg->bold_path != NULL)
    {
        if(FT_New_Face(lib, cfg->bold_path, 0, &bold) == 0)
        {
            if(FT_Set_Pixel_Sizes(bold, 0, cfg->font_px) != 0)
            {
                FT_Done_Face(bold);
                bold = NULL;
            }
        }
        else
        {
            bold = NULL;
        }
    }

    ascender  = (int)(face->size->metrics.ascender  >> 6);
    descender = (int)(face->size->metrics.descender >> 6);    /* negative */

    cell_h = ascender - descender;
    if(cell_h <= 0) cell_h = cfg->font_px;

    /* cell width from the advance of 'M', else the face's max advance */
    if(FT_Load_Char(face, 'M', FT_LOAD_DEFAULT) == 0)
        cell_w = (int)(face->glyph->advance.x >> 6);
    else
        cell_w = (int)(face->size->metrics.max_advance >> 6);
    if(cell_w <= 0) cell_w = (cfg->font_px + 1) / 2;

    img_w = grid->cols * cell_w;
    img_h = grid->rows * cell_h;
    if(img_w <= 0 || img_h <= 0) { rc = VWM_SHOT_ERR_ALLOC; goto done; }

    img = (uint8_t *)malloc((size_t)img_w * img_h * 3);
    if(img == NULL) { rc = VWM_SHOT_ERR_ALLOC; goto done; }

    for(row = 0; row < grid->rows; row++)
    {
        for(col = 0; col < grid->cols; col++)
        {
            const scrshot_cell_t    *cell = &grid->cells[row * grid->cols + col];
            int                     cx = col * cell_w;
            int                     cy = row * cell_h;
            short                   pf = -1, pb = -1;
            rgb8_t                  fg, bg, swap;

            if(pair_content(cell->pair, &pf, &pb) != OK) { pf = -1; pb = -1; }

            fg = color_to_rgb(pf, s_palette[7]);
            bg = color_to_rgb(pb, s_palette[0]);

            /* (a) reverse video swaps fg/bg */
            if(cell->attrs & A_REVERSE)
            {
                swap = fg;
                fg = bg;
                bg = swap;
            }

            /* (f) dim blends fg toward bg at 50% */
            if(cell->attrs & A_DIM)
            {
                fg.r = (uint8_t)((fg.r + bg.r) / 2);
                fg.g = (uint8_t)((fg.g + bg.g) / 2);
                fg.b = (uint8_t)((fg.b + bg.b) / 2);
            }

            /* (b) fill the cell with the background */
            fill_rect(img, img_w, img_h, cx, cy, cell_w, cell_h, bg);

            /* (c,d) render the glyph (blanks need no glyph) */
            if(cell->wch != L' ' && cell->wch != 0)
            {
                FT_Face use = ((cell->attrs & A_BOLD) && bold != NULL)
                    ? bold : face;

                if(FT_Load_Char(use, (FT_ULong)cell->wch,
                    FT_LOAD_RENDER) == 0)
                {
                    int gx = cx + use->glyph->bitmap_left;
                    int gy = cy + ascender - use->glyph->bitmap_top;

                    blit_glyph(img, img_w, img_h, &use->glyph->bitmap,
                        gx, gy, fg);

                    /* emulate bold by double-striking 1px to the right */
                    if((cell->attrs & A_BOLD) && bold == NULL)
                        blit_glyph(img, img_w, img_h, &use->glyph->bitmap,
                            gx + 1, gy, fg);
                }
                /* a single missing glyph is non-fatal: leave the cell bg */
            }

            /* (e) underline: 1px line near the bottom of the cell */
            if(cell->attrs & A_UNDERLINE)
                draw_hline(img, img_w, img_h, cx, cy + cell_h - 2,
                    cell_w, fg);
        }
    }

    *out_rgb = img;
    if(out_w != NULL) *out_w = img_w;
    if(out_h != NULL) *out_h = img_h;
    img = NULL;     /* ownership transferred to caller */

done:
    if(img  != NULL) free(img);
    if(bold != NULL) FT_Done_Face(bold);
    if(face != NULL) FT_Done_Face(face);
    if(lib  != NULL) FT_Done_FreeType(lib);

    return rc;
}
