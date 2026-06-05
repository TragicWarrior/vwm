#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cups/cups.h>

#include "vwmprint.h"

int
vwmprint_list_printers(char ***names)
{
    cups_dest_t *dests = NULL;
    char        **list;
    int         num;
    int         count = 0;
    int         i;

    if(names == NULL) return -1;
    *names = NULL;

    num = cupsGetDests(&dests);
    if(num <= 0)
    {
        if(dests != NULL) cupsFreeDests(num, dests);
        return 0;
    }

    list = (char **)calloc((size_t)num, sizeof(char *));
    if(list == NULL)
    {
        cupsFreeDests(num, dests);
        return -1;
    }

    for(i = 0; i < num; i++)
    {
        /* skip per-instance variants; list each base destination once */
        if(dests[i].instance != NULL) continue;
        if(dests[i].name == NULL) continue;

        list[count] = strdup(dests[i].name);
        if(list[count] != NULL) count++;
    }

    cupsFreeDests(num, dests);

    if(count == 0)
    {
        free(list);
        return 0;
    }

    *names = list;

    return count;
}

void
vwmprint_free_printers(char **names, int count)
{
    int i;

    if(names == NULL) return;

    for(i = 0; i < count; i++)
        free(names[i]);

    free(names);
}

int
vwmprint_print(const char *printer, const char *file, char *msg, size_t msgsz)
{
    const char  *base;
    const char  *slash;
    int         job;

    if(printer == NULL || file == NULL)
    {
        if(msg != NULL) snprintf(msg, msgsz, "Print failed: invalid request.");
        return -1;
    }

    slash = strrchr(file, '/');
    base = (slash != NULL) ? slash + 1 : file;

    job = cupsPrintFile(printer, file, base, 0, NULL);

    if(msg != NULL)
    {
        if(job > 0)
            snprintf(msg, msgsz, "Sent \"%s\" to %s  (job %d).",
                base, printer, job);
        else
            snprintf(msg, msgsz, "Print failed: %s", cupsLastErrorString());
    }

    return job;
}
