#include <string.h>
#include <stdlib.h>

#include "utf8_wide.h"

// 'max' will always allocate that amount when not 0.
wchar_t *
str_to_wchar_len(const char *str, int max)
{
    wchar_t         *wc;
    mbstate_t       ps;
    const char      *p = str;
    size_t          len = max;

    memset(&ps, 0, sizeof(mbstate_t));

    if(!len)
        len = mbsrtowcs(NULL, &p, 0, &ps);

    wc = calloc(len + 1, sizeof (wchar_t));
    p = str;

    memset(&ps, 0, sizeof (mbstate_t));
    len = mbsrtowcs(wc, &p, len, &ps);

    return wc;
}

wchar_t *
str_to_wchar(const char *str)
{
    return str_to_wchar_len (str, 0);
}

