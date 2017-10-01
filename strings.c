/*
 * Copyright (C) 2013 Bryan Christ <bryan.christ@mediafire.com>
 *               2014 Johannes Schauer <j.schauer@email.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#define _POSIX_C_SOURCE 200809L // for getline

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "strings.h"

char*
strdup_printf(char *fmt, ...)
{
    char           *ret_str = NULL;
    va_list         ap;
    int             bytes_to_allocate;

    va_start(ap, fmt);
    bytes_to_allocate = vsnprintf(ret_str, 0, fmt, ap);
    va_end(ap);

    // Add one for '\0'
    bytes_to_allocate++;

    ret_str = (char *)malloc(bytes_to_allocate * sizeof(char));
    if (ret_str == NULL) {
        fprintf(stderr, "failed to allocate memory\n");
        return NULL;
    }

    va_start(ap, fmt);
    bytes_to_allocate = vsnprintf(ret_str, bytes_to_allocate, fmt, ap);
    va_end(ap);

    return ret_str;
}

char**
strsplitv(char *string, char *delim)
{
    char            **array;
    char            *pos;
    char            *start;
    int             i = 0;
    unsigned int    delim_len;

    if(string == NULL) return NULL;
    if(delim == NULL) return NULL;

    delim_len = strlen(delim);

    if(strlen(string) < delim_len) return NULL;

    pos = string;
    do
    {
        pos = strstr(pos, delim);
        if(pos != NULL)
        {
            pos += delim_len;
            i++;
        }
    }
    while(pos != NULL);

    array = (char**)calloc(i + 2, sizeof(char*));

    i = 0;
    start = string;

    do
    {
        pos = strstr(start, delim);

        if(pos != NULL)
        {
            array[i] = strndup(start, (pos - start));
            start = pos;
            start += delim_len;
            i++;
        }
    }
    while(pos != NULL);


    if(array[i] == NULL) array[i] = strdup(start);

    return array;
}

char**
strdupv(char **array)
{
    int     i = 0;
    char    **retval;

    if(array == NULL) return NULL;

    // how many items are there?
    while(array[i]) i++;

    // alloc first dimension
    retval = (char**)calloc(i + 1, sizeof(char*));

    i = 0;
    while(array[i])
    {
        retval[i] = strdup(array[i]);
        i++;
    }

    return retval;
}

void
strfreev(char **array)
{
    char    **rewind = NULL;

    if(array == NULL) return;

    rewind = array;

    while(*array != NULL)
    {
        free(*array);
        array++;
    }

    free(rewind);

    return;
}
