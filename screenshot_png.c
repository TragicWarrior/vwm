#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "screenshot_priv.h"

/*
    A small, dependency-free PNG writer.  The image data is emitted as a
    zlib stream made of DEFLATE "stored" (uncompressed) blocks, so no
    external compression library is required.  Files are larger than a
    compressed PNG but are fully valid and open everywhere.
*/

/* ── CRC32 (PNG chunk checksums) ────────────────────────────────────────── */

static uint32_t s_crc_table[256];
static int      s_crc_ready = 0;

static void
crc_init(void)
{
    uint32_t    c;
    int         n, k;

    if(s_crc_ready) return;

    for(n = 0; n < 256; n++)
    {
        c = (uint32_t)n;
        for(k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_crc_table[n] = c;
    }

    s_crc_ready = 1;
}

static uint32_t
crc_feed(uint32_t crc, const uint8_t *buf, size_t len)
{
    size_t i;

    for(i = 0; i < len; i++)
        crc = s_crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);

    return crc;
}

/* ── Adler32 (zlib stream checksum) ─────────────────────────────────────── */

static uint32_t
adler32(const uint8_t *data, size_t len)
{
    uint32_t    a = 1, b = 0;
    size_t      i;

    for(i = 0; i < len; i++)
    {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }

    return (b << 16) | a;
}

/* ── output helpers ─────────────────────────────────────────────────────── */

static int
write_be32(FILE *f, uint32_t v)
{
    uint8_t b[4];

    b[0] = (uint8_t)((v >> 24) & 0xFF);
    b[1] = (uint8_t)((v >> 16) & 0xFF);
    b[2] = (uint8_t)((v >>  8) & 0xFF);
    b[3] = (uint8_t)( v        & 0xFF);

    return (fwrite(b, 1, 4, f) == 4) ? 0 : -1;
}

static int
write_chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint32_t crc;

    if(write_be32(f, (uint32_t)len) != 0) return -1;
    if(fwrite(type, 1, 4, f) != 4) return -1;
    if(len > 0 && fwrite(data, 1, len, f) != len) return -1;

    crc_init();
    crc = 0xFFFFFFFFu;
    crc = crc_feed(crc, (const uint8_t *)type, 4);
    if(len > 0) crc = crc_feed(crc, data, len);
    crc ^= 0xFFFFFFFFu;

    return write_be32(f, crc);
}

/* wrap raw bytes in a zlib stream of stored DEFLATE blocks */
static uint8_t*
zlib_store(const uint8_t *raw, size_t raw_len, size_t *out_len)
{
    size_t      nblocks;
    size_t      zlen;
    size_t      zp = 0;
    size_t      off = 0;
    uint8_t     *z;
    uint32_t    ad;

    nblocks = (raw_len + 65534u) / 65535u;
    if(nblocks == 0) nblocks = 1;

    zlen = 2 + nblocks * 5 + raw_len + 4;

    z = (uint8_t *)malloc(zlen);
    if(z == NULL) return NULL;

    z[zp++] = 0x78;     /* CMF: deflate, 32K window */
    z[zp++] = 0x01;     /* FLG: no preset dict, check bits valid */

    do
    {
        size_t  chunk = raw_len - off;
        int     final;

        if(chunk > 65535u) chunk = 65535u;
        final = (off + chunk >= raw_len) ? 1 : 0;

        z[zp++] = (uint8_t)final;                       /* BFINAL, BTYPE=00 */
        z[zp++] = (uint8_t)( chunk        & 0xFF);      /* LEN  (LE) */
        z[zp++] = (uint8_t)((chunk >>  8) & 0xFF);
        z[zp++] = (uint8_t)((~chunk)      & 0xFF);      /* NLEN (LE) */
        z[zp++] = (uint8_t)((~chunk >> 8) & 0xFF);

        if(chunk > 0)
        {
            memcpy(z + zp, raw + off, chunk);
            zp += chunk;
        }

        off += chunk;

        if(final) break;
    }
    while(off < raw_len);

    ad = adler32(raw, raw_len);
    z[zp++] = (uint8_t)((ad >> 24) & 0xFF);
    z[zp++] = (uint8_t)((ad >> 16) & 0xFF);
    z[zp++] = (uint8_t)((ad >>  8) & 0xFF);
    z[zp++] = (uint8_t)( ad        & 0xFF);

    *out_len = zp;

    return z;
}

/* ── public writer ──────────────────────────────────────────────────────── */

int
vwmscrshot_write_png(const char *path, const uint8_t *rgb,
    int width, int height)
{
    FILE        *f = NULL;
    uint8_t     *raw = NULL;
    uint8_t     *z = NULL;
    size_t      raw_len;
    size_t      zlen = 0;
    uint8_t     ihdr[13];
    int         rc = VWM_SHOT_ERR_PNG;
    int         y;
    size_t      pos = 0;

    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

    if(path == NULL || rgb == NULL || width <= 0 || height <= 0)
        return VWM_SHOT_ERR_PNG;

    /* build filtered scanlines: each row prefixed with filter type 0 */
    raw_len = (size_t)height * (1 + (size_t)width * 3);
    raw = (uint8_t *)malloc(raw_len);
    if(raw == NULL) { rc = VWM_SHOT_ERR_ALLOC; goto done; }

    for(y = 0; y < height; y++)
    {
        raw[pos++] = 0;
        memcpy(raw + pos, rgb + (size_t)y * width * 3, (size_t)width * 3);
        pos += (size_t)width * 3;
    }

    z = zlib_store(raw, raw_len, &zlen);
    if(z == NULL) { rc = VWM_SHOT_ERR_ALLOC; goto done; }

    /* IHDR payload */
    ihdr[0]  = (uint8_t)((width  >> 24) & 0xFF);
    ihdr[1]  = (uint8_t)((width  >> 16) & 0xFF);
    ihdr[2]  = (uint8_t)((width  >>  8) & 0xFF);
    ihdr[3]  = (uint8_t)( width         & 0xFF);
    ihdr[4]  = (uint8_t)((height >> 24) & 0xFF);
    ihdr[5]  = (uint8_t)((height >> 16) & 0xFF);
    ihdr[6]  = (uint8_t)((height >>  8) & 0xFF);
    ihdr[7]  = (uint8_t)( height        & 0xFF);
    ihdr[8]  = 8;       /* bit depth          */
    ihdr[9]  = 2;       /* color type: RGB    */
    ihdr[10] = 0;       /* compression        */
    ihdr[11] = 0;       /* filter             */
    ihdr[12] = 0;       /* interlace          */

    f = fopen(path, "wb");
    if(f == NULL) { rc = VWM_SHOT_ERR_PNG; goto done; }

    if(fwrite(sig, 1, 8, f) != 8)                 { rc = VWM_SHOT_ERR_PNG; goto done; }
    if(write_chunk(f, "IHDR", ihdr, 13) != 0)     { rc = VWM_SHOT_ERR_PNG; goto done; }
    if(write_chunk(f, "IDAT", z, zlen) != 0)      { rc = VWM_SHOT_ERR_PNG; goto done; }
    if(write_chunk(f, "IEND", NULL, 0) != 0)      { rc = VWM_SHOT_ERR_PNG; goto done; }

    rc = VWM_SHOT_OK;

done:
    if(f != NULL) fclose(f);
    free(z);
    free(raw);

    return rc;
}
