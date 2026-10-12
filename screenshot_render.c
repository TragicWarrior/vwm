#include <stdlib.h>
#include <string.h>

#include <ncursesw/curses.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "screenshot_priv.h"

/* an RGB colour as the image holds it.  A cell's colours arrive as
   0x00RRGGBB, already final: the palette lookup, reverse video and dim
   are all done by libviper's decode (vdk_cells_read_row), so nothing
   here consults ncurses. */
typedef struct { uint8_t r, g, b; } rgb8_t;

/* unpack a cell's 0x00RRGGBB colour */
static rgb8_t
cell_rgb(uint32_t rgb)
{
    rgb8_t  c;

    c.r = (uint8_t)VDK_CELL_R(rgb);
    c.g = (uint8_t)VDK_CELL_G(rgb);
    c.b = (uint8_t)VDK_CELL_B(rgb);

    return c;
}

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
            const vdk_cell_t    *cell = &grid->cells[row * grid->cols + col];
            int                 cx = col * cell_w;
            int                 cy = row * cell_h;
            int                 span = cell_w;
            int                 bold_cell;
            rgb8_t              fg, bg;

            /* the second half of a double-width character: its first
               half drew both cells */
            if(cell->flags & VDK_CELL_WIDE_TAIL) continue;

            /* a double-width character covers this cell and the next */
            if(cell->flags & VDK_CELL_WIDE) span = cell_w * 2;

            /* (a) the colours, final as they come */
            fg = cell_rgb(cell->fg);
            bg = cell_rgb(cell->bg);
            bold_cell = (cell->attrs & VDK_CELL_BOLD) ? 1 : 0;

            /* (b) fill the cell with the background */
            fill_rect(img, img_w, img_h, cx, cy, span, cell_h, bg);

            /* (c,d) render the glyph (blanks need no glyph) */
            if(cell->ch != ' ' && cell->ch != 0)
            {
                FT_Face use = (bold_cell && bold != NULL) ? bold : face;

                if(FT_Load_Char(use, (FT_ULong)cell->ch,
                    FT_LOAD_RENDER) == 0)
                {
                    int gx = cx + use->glyph->bitmap_left;
                    int gy = cy + ascender - use->glyph->bitmap_top;

                    blit_glyph(img, img_w, img_h, &use->glyph->bitmap,
                        gx, gy, fg);

                    /* emulate bold by double-striking 1px to the right */
                    if(bold_cell && bold == NULL)
                        blit_glyph(img, img_w, img_h, &use->glyph->bitmap,
                            gx + 1, gy, fg);
                }
                /* a single missing glyph is non-fatal: leave the cell bg */
            }

            /* (e) underline: 1px line near the bottom of the cell */
            if(cell->attrs & VDK_CELL_UNDERLINE)
                draw_hline(img, img_w, img_h, cx, cy + cell_h - 2,
                    span, fg);
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
