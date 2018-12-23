#ifndef _VWM_UTF8_WIDE_
#define _VWM_UTF8_WIDE_

#include <wchar.h>

wchar_t*    str_to_wchar(const char *str);

wchar_t*    str_to_wchar_len(const char *str, int max);

#endif
