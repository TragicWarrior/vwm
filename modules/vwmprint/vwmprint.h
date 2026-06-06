#ifndef _VWMPRINT_H_
#define _VWMPRINT_H_

#include <stddef.h>

/*
    Thin wrapper over the CUPS client API used by the print tool.
*/

/*
    list the configured CUPS destinations.  on success returns the count
    and stores a malloc'd array of strdup'd printer names in *names (free
    with vwmprint_free_printers()).  returns 0 if none are configured,
    or -1 on error.
*/
int     vwmprint_list_printers(char ***names);

/* release an array returned by vwmprint_list_printers() */
void    vwmprint_free_printers(char **names, int count);

/*
    print file to the named printer.  returns the CUPS job id (> 0) on
    success or <= 0 on failure; a human-readable result is written to msg.
*/
int     vwmprint_print(const char *printer, const char *file,
            char *msg, size_t msgsz);

#endif
