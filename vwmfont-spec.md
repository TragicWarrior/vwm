# Build the `vwmfont` loadable module

Build a self-contained C module — `vwmfont.h` + `vwmfont.c` — that renders a text string as large "pixel-art" text by using a **Terminus PSF console font** as the glyph source. Each *on* pixel in a glyph's bitmap becomes one filled terminal cell; each *off* pixel becomes one blank cell. The rendered result is written into a freshly created vk_widget_t, which the entry-point function returns to the caller.

This is the "big fonts" module feature for the **vwm** project: the user picks one of Terminus's fixed grid sizes, and that single choice sets both the resolution and the on-screen size of the hostname text (superseding basic text). `vwmfont` does all of the heavy lifting (font discovery, PSF parsing, glyph lookup, cell painting, window sizing).

## Aspect ratio — by design

Terminal cells are roughly twice as tall as they are wide, so mapping one font pixel to one cell makes text look proportionally tall. **This is intentional.** The module performs no aspect-ratio correction; the user controls proportions by choosing the grid (e.g. an 8×16 grid renders shorter than 16×32). Do **not** add half-block vertical packing in this module — see *Out of scope*.

## Public API

The primary entry point:

```c
vk_widget_t *vwmfont_render(const char *utf8_text, vwmfont_size_t size);
```

- `utf8_text` — the string to render, UTF-8 encoded. An embedded newline (`\n`) starts a new row of glyphs.
- `size` — selects which Terminus grid (and therefore which PSF file) to use. See *Size specifiers*.
- **Returns** — a newly allocated `vk_widget_t`, sized exactly to fit the rendered text, with the bitmap text painted into it. Returns `NULL` on any failure (font not found, parse error, allocation failure, empty/whitespace-only input).

Optionally provide init/teardown so fonts are located and parsed once and cached:

```c
int  vwmfont_init(const char *font_dir);  /* NULL = search default dirs / $VWMFONT_DIR */
void vwmfont_shutdown(void);              /* free cached parsed fonts */
```

If `vwmfont_init` is never called, `vwmfont_render` must lazily locate and load the needed PSF on first use.

## Size specifiers

Expose an enum covering all nine Terminus grids, with weight where it exists. Weights: normal `n` and bold `b` for everything except 6×12 (normal only); plus CRT/VGA-bold `v` for 8×14 and 8×16.

| Grid (W×H px) | Weights | Example token |
|---|---|---|
| 6×12  | n            | `ter-112n` |
| 8×14  | n, b, v      | `ter-114n` / `114b` / `114v` |
| 8×16  | n, b, v      | `ter-116n` / `116b` / `116v` |
| 10×18 | n, b         | `ter-118n` |
| 10×20 | n, b         | `ter-120n` |
| 11×22 | n, b         | `ter-122n` |
| 12×24 | n, b         | `ter-124n` |
| 14×28 | n, b         | `ter-128n` |
| 16×32 | n, b         | `ter-132n` |

In the console token, the number is the pixel **height** and the suffix is the weight. Map each enum value to its `ter-*.psf.gz` filename. (Optionally also accept an arbitrary PSF path so non-Terminus fonts work, since PSF is a generic format — but only the Terminus grids are surfaced through the enum.)

## Font discovery

Search the standard console-font directories for the `ter-*.psf.gz` matching the requested size:

- `/usr/share/consolefonts/` (Debian/Ubuntu)
- `/usr/lib/kbd/consolefonts/` (Fedora/RHEL)
- `/usr/share/kbd/consolefonts/` (Arch)

Allow override via `vwmfont_init(font_dir)` or a `$VWMFONT_DIR` environment variable, both taking precedence over the defaults. PSF console fonts are not indexed by fontconfig, so directory search is the mechanism — there is no `fc-list` equivalent for them.

## PSF parsing

The files are gzip-compressed (`*.psf.gz`); inflate with zlib first. Support both PSF formats:

- **PSF1** — magic `0x36 0x04`. Width is always 8; `charsize` (1 byte) = bytes per glyph = height. The mode byte's flags indicate a 512-glyph set and/or a Unicode table. The table, if present, is UCS-2 (16-bit LE), entries terminated by `0xFFFF` with `0xFFFE` as the combining-sequence separator. (The 8-wide Terminus sizes may be PSF1.)
- **PSF2** — magic `0x72 0xB5 0x4A 0x86`, followed by LE uint32 fields: version, headersize, flags, length (glyph count), charsize, height, width. Bitmaps begin at `headersize`. The Unicode table (present iff the flags bit is set) follows the bitmaps and is UTF-8, terminated by `0xFF` with `0xFE` as the combining separator.

Per glyph: `bytes_per_row = (width + 7) / 8`, each row stored MSB-first. The pixel at column *x* of a row is bit `(7 - (x % 8))` of byte `(x / 8)`; a set bit means *on*. **Mask off the padding bits** — an 11-wide glyph occupies 2 bytes/row but only 11 bits are significant.

**Glyph lookup must go through the Unicode table**, not `index == codepoint`. Build a `codepoint → glyph index` map by walking the table. (It happens to line up for ASCII in Terminus, but the table is the correct path and is required the moment input contains `é`, box-drawing characters, Cyrillic, etc.) Decode `utf8_text` into codepoints, then resolve each via the map. For a codepoint with no glyph, fall back to a blank glyph (or the glyph for `?` / U+FFFD) rather than failing the whole render.

## Rendering

One font pixel → one terminal cell. Paint *every* cell of the vk_widget_t window canvas (both on and off) so the result is a clean filled rectangle.

Assuming that the module is installed, loaded, and running, the user should be able to select the fill character from a menu.  for now the default will be a full block.  other options for the fill character can be "O" or "X".

1. **`VWMFONT_FILL_FULLBLOCK` (default, recommended)** — U+2588 FULL BLOCK, drawn via a wide-char `cchar_t` with `mvwadd_wch`. Defined to fill the whole cell, so adjacent on-pixels merge into solid strokes. Requires `ncursesw` and a UTF-8 locale.
2. **`VWMFONT_FILL_REVSPACE` (most portable fallback)** — a space printed with `A_REVERSE` (or a background color pair). Depends on no glyph existing at all, so it works even where U+2588 renders as tofu. Ideal for monochrome on/off.

Off-pixels: write a normal blank (a space with default attributes).  In the Settings, the user should be able to choose the foreground and background color.   

## Errors

Return `NULL` (never crash) on: missing/unreadable font file, gzip or PSF parse failure, unsupported magic, allocation failure, empty or whitespace-only input, or computed dimensions that are zero or exceed a sane cap. Keep any internal diagnostics behind a debug flag; the library should not print to the screen.

## Build / dependencies

- C99, no compiler-specific extensions.
- `zlib` (to inflate `*.psf.gz`).
- Resolve flags via `pkg-config --cflags --libs ncursesw zlib`.
- The caller is responsible for `setlocale(LC_ALL, "")` and 


## Acceptance criteria

- Correctly parses both PSF1 (8-wide) and PSF2 (wider) Terminus files, gzip included.
- Renders ASCII plus at least Latin-1 and box-drawing characters resolved through the Unicode table.
