/* Minimal shim of tinyfiledialogs for selectFolderDialog on Linux using zenity if present. */
#include "tinyfiledialogs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int has_cmd(const char* cmd) {
    char buf[256];
    snprintf(buf, sizeof(buf), "command -v %s >/dev/null 2>&1", cmd);
    return system(buf) == 0;
}

const char * tinyfd_selectFolderDialog(const char * aTitle, const char * aDefaultPath) {
    (void)aTitle;
    static char out[1024];
    out[0] = '\0';
    const char * title = aTitle && aTitle[0] ? aTitle : "Select Folder";
    const char * def = aDefaultPath && aDefaultPath[0] ? aDefaultPath : "";
    if (has_cmd("zenity")) {
        char cmd[1600];
        snprintf(cmd, sizeof(cmd),
                 "zenity --file-selection --directory --title=%s --filename=%s 2>/dev/null",
                 title, def);
        FILE* f = popen(cmd, "r");
        if (!f) return NULL;
        if (fgets(out, sizeof(out), f) == NULL) { pclose(f); return NULL; }
        pclose(f);
        size_t n = strlen(out); if (n && out[n-1]=='\n') out[n-1] = '\0';
        return out;
    }
    return NULL; /* no dialog available */
}
